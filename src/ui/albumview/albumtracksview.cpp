#include "albumview/albumtracksview.h"

#include <QPointer>
#include <QScrollBar>

#include "covers/covercache.h"

#include <algorithm>
#include <functional>
#include <utility>

#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSet>
#include <QStyle>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

// Contract: docs/spec/album-view.md (AV-VIEW, AV-SELECT, AV-ACTIVATE, AV-BAR).
namespace linuxbee::ui {

namespace {

// What a row carries in this role: a track row, the id of its track. A
// heading carries nothing: its place is its place in the tree. A disc row
// carries nothing.
constexpr int ValueRole = Qt::UserRole;

QString firstValue(const LibraryTrack &track, const QString &field)
{
    return track.tags.fields.value(field).value(0).trimmed();
}

// AV-ACTIVATE: what the user did to a row, read from the events themselves: a
// double click is the double-click event, and the keys are Return and Enter on
// the current row. Qt's own "activated" is not used: on some styles a single
// click makes it fire, and a view takes a double-click event for a double
// click only after a press on the same row.
class ActivationFilter : public QObject {
public:
    ActivationFilter(
        QTreeWidget *tree, std::function<void(QTreeWidgetItem *)> activate, QObject *parent)
        : QObject(parent)
        , m_tree(tree)
        , m_activate(std::move(activate))
    {
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::MouseButtonDblClick && watched == m_tree->viewport()) {
            const auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() != Qt::LeftButton)
                return false;
            if (QTreeWidgetItem *item = m_tree->itemAt(mouse->position().toPoint()))
                m_activate(item);
            return true;
        }
        if (event->type() == QEvent::KeyPress && watched == m_tree) {
            const int key = static_cast<QKeyEvent *>(event)->key();
            QTreeWidgetItem *item = m_tree->currentItem();
            if ((key == Qt::Key_Return || key == Qt::Key_Enter) && item) {
                m_activate(item);
                return true;
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QTreeWidget *m_tree;
    std::function<void(QTreeWidgetItem *)> m_activate;
};

// AV-COVER: tells the view that its size or its visibility changed.
class GeometryFilter : public QObject {
public:
    GeometryFilter(std::function<void()> changed, QObject *parent)
        : QObject(parent)
        , m_changed(std::move(changed))
    {
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Show)
            m_changed();
        return QObject::eventFilter(watched, event);
    }

private:
    std::function<void()> m_changed;
};

// AV-COVER: what a heading keeps of the picture it shows (its cache key), so
// that the same picture is not set twice.
constexpr int PictureRole = Qt::UserRole + 1;

}

struct AlbumTracksView::Private {
    AlbumTracksView *q = nullptr;
    QWidget *bar = nullptr; // jumpBar
    QTreeWidget *tree = nullptr;
    QList<AlbumGroup> groups;
    QPointer<CoverCache> covers; // AV-COVER
    bool askQueued = false; // AV-COVER: the run that asks for covers is already on its way
    QList<qsizetype> firstAt; // where each group's first track stands in `shown`
    QList<LibraryTrack> shown; // the tracks the tree was filled from (shared, not copied)
    QHash<qint64, QTreeWidgetItem *> trackRows; // the rows of the albums in the tree
    QList<qint64> announced; // selectedTrackIds() as the listeners last heard it
    int built = 0; // AV-VIEW: the headings built
    int fills = 0; // AV-VIEW: the calls of setTracks, each of which built the rows
    // True while the view itself moves the selection, so that the tree's
    // signal is not read back as the user's choice.
    bool quiet = false;

    // AV-SELECT: every selected track row, and every track of a selected
    // heading, in the order shown and each once.
    QList<qint64> selectedIds() const
    {
        QSet<const QTreeWidgetItem *> headings;
        QSet<qint64> tracks;
        for (const QTreeWidgetItem *item : tree->selectedItems()) {
            if (!item->parent())
                headings.insert(item);
            else
                tracks.insert(item->data(0, ValueRole).toLongLong());
        }
        QList<qint64> ids;
        if (headings.isEmpty() && tracks.isEmpty())
            return ids;
        QSet<qint64> seen;
        for (int g = 0; g < groups.size(); ++g) {
            const bool whole = headings.contains(tree->topLevelItem(g));
            for (const qint64 id : groups.at(g).trackIds) {
                if ((whole || tracks.contains(id)) && !seen.contains(id)) {
                    seen.insert(id);
                    ids.append(id);
                }
            }
        }
        return ids;
    }

    // Tells the listeners, once, if the answer of selectedIds() is another one.
    void announce()
    {
        const QList<qint64> now = selectedIds();
        if (now == announced)
            return;
        announced = now;
        emit q->selectionChanged();
    }

    // AV-ACTIVATE: a heading plays its album, a track row its track; a disc
    // row (it cannot be selected) does nothing.
    void activate(QTreeWidgetItem *item)
    {
        if (!item->parent())
            emit q->albumActivated(tree->indexOfTopLevelItem(item));
        else if (item->flags() & Qt::ItemIsSelectable)
            emit q->trackActivated(item->data(0, ValueRole).toLongLong());
    }

    // Selects exactly the rows of these tracks, in one call: a call per row
    // costs time in proportion to the rows already selected.
    void selectRowsOf(const QList<qint64> &ids)
    {
        QItemSelection selection;
        for (const qint64 id : ids) {
            if (const QTreeWidgetItem *row = trackRows.value(id, nullptr)) {
                const QModelIndex index = tree->indexFromItem(row);
                selection.select(index, index);
            }
        }
        tree->selectionModel()->select(
            selection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    }

    QTreeWidgetItem *trackRow(const LibraryTrack &track) const
    {
        const LibraryFieldSource source(track);
        QString title = source.value(QStringLiteral("Title")).value_or(QString());
        if (title.isEmpty())
            title = QFileInfo(track.path).fileName();
        auto *row = new QTreeWidgetItem;
        row->setText(0, source.value(QStringLiteral("Track#")).value_or(QString()));
        row->setText(1, title);
        row->setText(2, source.value(QStringLiteral("Time")).value_or(QString()));
        row->setTextAlignment(0, Qt::AlignRight | Qt::AlignVCenter);
        row->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
        for (int column = 0; column < 3; ++column)
            row->setToolTip(column, track.path);
        row->setData(0, ValueRole, track.id);
        return row;
    }

    // AV-VIEW, One change, one album: what an album's rows show is the same
    // when its group is equal and each track has the id, path, tags, format
    // and length it had. The tags are Qt containers that share their data, so
    // tracks that were not touched compare equal without a look at the text.
    static bool sameRows(const LibraryTrack &a, const LibraryTrack &b)
    {
        return a.id == b.id && a.path == b.path && a.tags.fields == b.tags.fields
            && a.tags.format == b.tags.format && a.tags.audio.durationMs == b.tags.audio.durationMs;
    }

    // AV-COVER: with a cache, every heading is as high as a cover (and its two
    // lines of text), with or without a picture, so a cover that arrives moves
    // no row. Without a cache, 0: the row has the height its text gives it.
    int headingHeight(const QFont &headingFont) const
    {
        if (covers.isNull())
            return 0;
        const int margin
            = 2 * (tree->style()->pixelMetric(QStyle::PM_FocusFrameVMargin, nullptr, tree) + 1);
        const int text = 2 * QFontMetrics(headingFont).lineSpacing() + margin;
        return std::max(covers->edge() + margin, text);
    }

    // AV-COVER: the run that asks the cache for the headings in view and sets
    // the icons it gives. Asked for now, it happens at the next turn of the
    // event loop, once, however many asks came before it.
    void askSoon()
    {
        if (askQueued || covers.isNull())
            return;
        askQueued = true;
        QMetaObject::invokeMethod(
            q,
            [this] {
                askQueued = false;
                askForHeadings();
            },
            Qt::QueuedConnection);
    }

    // The icon of one heading, from what the cache has for its album's first
    // track. A heading keeps the picture it has until the cache has another.
    void showCoverOf(int group)
    {
        QTreeWidgetItem *heading = tree->topLevelItem(group);
        if (!heading || group >= firstAt.size())
            return;
        const LibraryTrack &first = shown.at(firstAt.at(group));
        const qint64 stamp = first.modified.isValid() ? first.modified.toMSecsSinceEpoch() : 0;
        const QImage image = covers->cover(first.path, stamp);
        if (image.isNull()) {
            // Looked up and has none: no icon. Not kept yet: what is there stays.
            if (covers->has(first.path, stamp) && !heading->icon(0).isNull()) {
                heading->setIcon(0, QIcon());
                heading->setData(0, PictureRole, QVariant());
            }
            return;
        }
        if (heading->data(0, PictureRole).toLongLong() == image.cacheKey())
            return;
        heading->setIcon(0, QIcon(QPixmap::fromImage(image)));
        heading->setData(0, PictureRole, qint64(image.cacheKey()));
    }

    // AV-COVER: from the heading at the top of the view downwards, stopping
    // at the first heading below it. The rows of the other albums are not
    // looked at, so this costs the same with thousands of albums.
    void askForHeadings()
    {
        if (covers.isNull() || !q->isVisible())
            return;
        const QModelIndex top = tree->indexAt(QPoint(0, 0));
        if (!top.isValid())
            return;
        const QModelIndex topLevel = top.parent().isValid() ? top.parent() : top;
        const int bottom = tree->viewport()->height();
        const int count = tree->topLevelItemCount();
        for (int group = topLevel.row(); group < count; ++group) {
            const QRect at = tree->visualItemRect(tree->topLevelItem(group));
            if (at.top() >= bottom)
                break;
            if (at.bottom() >= 0)
                showCoverOf(group);
        }
    }

    // The rows of one album: its heading with the disc and track rows under
    // it, not yet in the tree. The disc and heading rows are added to `spanned`.
    QTreeWidgetItem *albumRows(const AlbumGroup &group, const QList<LibraryTrack> &tracks,
        qsizetype first, const QFont &headingFont, QList<QTreeWidgetItem *> *spanned)
    {
        auto *heading = new QTreeWidgetItem;
        ++built;
        heading->setText(0, albumHeading(group) + QLatin1Char('\n') + albumDetail(group));
        heading->setFont(0, headingFont);
        if (const int height = headingHeight(headingFont))
            heading->setSizeHint(0, QSize(0, height));

        // The rows of the album are made apart and handed over in one call.
        QList<QTreeWidgetItem *> children;
        spanned->append(heading);
        int nextDisc = 0;
        for (int i = 0; i < group.trackIds.size(); ++i) {
            const LibraryTrack &track = tracks.at(first + i);
            if (nextDisc < group.discStarts.size() && group.discStarts.at(nextDisc) == i) {
                auto *disc = new QTreeWidgetItem;
                disc->setText(0,
                    AlbumTracksView::tr("Disc %1").arg(firstValue(track, QStringLiteral("Disc#"))));
                disc->setFlags(Qt::ItemIsEnabled); // it cannot be selected
                children.append(disc);
                spanned->append(disc);
                ++nextDisc;
            }
            QTreeWidgetItem *row = trackRow(track);
            trackRows.insert(track.id, row);
            children.append(row);
        }
        heading->addChildren(children);
        return heading;
    }

    // AV-VIEW: the tree, from the groups of these tracks. The albums at the
    // head and at the tail that are as they were keep their rows; the albums
    // between are taken out and built again.
    void fill(const QList<LibraryTrack> &tracks)
    {
        const QList<AlbumGroup> oldGroups = groups;
        const QList<LibraryTrack> oldTracks = shown;
        groups = albumGroups(tracks);
        shown = tracks;

        // Where each album's first track stands in the old and the new list.
        const auto starts = [](const QList<AlbumGroup> &list) {
            QList<qsizetype> at;
            at.reserve(list.size());
            qsizetype first = 0;
            for (const AlbumGroup &group : list) {
                at.append(first);
                first += group.trackIds.size();
            }
            return at;
        };
        const QList<qsizetype> oldAt = starts(oldGroups);
        const QList<qsizetype> newAt = starts(groups);
        firstAt = newAt;
        const auto same = [&](qsizetype o, qsizetype n) {
            if (oldGroups.at(o) != groups.at(n))
                return false;
            for (qsizetype i = 0; i < groups.at(n).trackIds.size(); ++i) {
                if (!sameRows(oldTracks.at(oldAt.at(o) + i), tracks.at(newAt.at(n) + i)))
                    return false;
            }
            return true;
        };
        const qsizetype oldCount = oldGroups.size();
        const qsizetype newCount = groups.size();
        const qsizetype most = std::min(oldCount, newCount);
        qsizetype head = 0;
        while (head < most && same(head, head))
            ++head;
        qsizetype tail = 0;
        while (head + tail < most && same(oldCount - 1 - tail, newCount - 1 - tail))
            ++tail;

        // The albums between, in the old tree, are taken out.
        const qsizetype goneEnd = oldCount - tail;
        if (head < goneEnd) {
            for (qsizetype g = head; g < goneEnd; ++g) {
                for (const qint64 id : oldGroups.at(g).trackIds)
                    trackRows.remove(id);
            }
            if (head == 0 && tail == 0) {
                tree->clear();
            } else {
                for (qsizetype g = goneEnd; g > head; --g)
                    delete tree->takeTopLevelItem(int(head));
            }
        }

        // The albums between, in the new list, are built and put in at once.
        QFont headingFont = tree->font();
        headingFont.setBold(true);
        QList<QTreeWidgetItem *> spanned; // the headings and disc rows built
        QList<QTreeWidgetItem *> headings;
        for (qsizetype g = head; g < newCount - tail; ++g)
            headings.append(albumRows(groups.at(g), tracks, newAt.at(g), headingFont, &spanned));
        if (headings.isEmpty())
            return;
        tree->insertTopLevelItems(int(head), headings);
        // AV-VIEW, Speed: every heading and disc row built is made to span the
        // columns in one pass, once they are in the tree (spanning works only
        // for items that are in the tree), and the headings are expanded in
        // one step when the whole tree is new, not one by one as each is added.
        for (QTreeWidgetItem *item : std::as_const(spanned))
            item->setFirstColumnSpanned(true);
        if (head == 0 && tail == 0) {
            tree->expandAll();
        } else {
            for (QTreeWidgetItem *item : std::as_const(headings))
                item->setExpanded(true);
        }
    }
};

AlbumTracksView::AlbumTracksView(QWidget *parent)
    : QWidget(parent)
    , d(new Private)
{
    d->q = this;

    d->tree = new QTreeWidget(this);
    d->tree->setObjectName(QStringLiteral("albumTree"));
    d->tree->setColumnCount(3);
    d->tree->setHeaderHidden(true);
    d->tree->setRootIsDecorated(false); // no branch marks
    d->tree->setItemsExpandable(false); // an album does not fold
    d->tree->setExpandsOnDoubleClick(false);
    d->tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    d->tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    QHeaderView *header = d->tree->header();
    header->setStretchLastSection(false);
    // AV-VIEW, Speed: no column is sized to its contents, because that measures
    // every row at every fill. The title stretches; the other two have a width
    // worked out once, here, from the font: room for a track number and for a
    // time such as 00:00:00, plus the margin the item delegate puts either side.
    header->setSectionResizeMode(0, QHeaderView::Fixed);
    header->setSectionResizeMode(1, QHeaderView::Stretch);
    header->setSectionResizeMode(2, QHeaderView::Fixed);
    const QFontMetrics metrics(d->tree->font());
    const int margin
        = 2 * (d->tree->style()->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, d->tree) + 1);
    header->resizeSection(0, metrics.horizontalAdvance(QStringLiteral("0000")) + margin);
    header->resizeSection(2, metrics.horizontalAdvance(QStringLiteral("00:00:00")) + margin);

    // AV-BAR: one button for "#" and one for each letter; a click jumps.
    d->bar = new QWidget(this);
    d->bar->setObjectName(QStringLiteral("jumpBar"));
    auto *barLayout = new QHBoxLayout(d->bar);
    barLayout->setContentsMargins(0, 0, 0, 0);
    barLayout->setSpacing(0);
    for (const QChar letter : QStringLiteral("#ABCDEFGHIJKLMNOPQRSTUVWXYZ")) {
        auto *button = new QToolButton(d->bar);
        button->setObjectName(QStringLiteral("jump_") + letter);
        button->setText(QString(letter));
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus); // the list keeps the keyboard
        barLayout->addWidget(button);
        connect(button, &QToolButton::clicked, this, [this, letter]() { jumpTo(letter); });
    }
    barLayout->addStretch(1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(d->bar);
    layout->addWidget(d->tree, 1);

    connect(d->tree, &QTreeWidget::itemSelectionChanged, this, [this]() {
        if (!d->quiet)
            d->announce();
    });

    // AV-COVER: the covers are asked for again when the view is scrolled,
    // resized or shown.
    connect(
        d->tree->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() { d->askSoon(); });
    auto *geometry = new GeometryFilter([this]() { d->askSoon(); }, this);
    d->tree->viewport()->installEventFilter(geometry);
    installEventFilter(geometry);

    // AV-ACTIVATE: a double click, or Return or Enter on the current row.
    auto *filter
        = new ActivationFilter(d->tree, [this](QTreeWidgetItem *item) { d->activate(item); }, this);
    d->tree->viewport()->installEventFilter(filter);
    d->tree->installEventFilter(filter);

    // AV-ACTIVATE: a right click on a track row or a heading. A row that was
    // not selected becomes the selection first; the view shows no menu itself.
    d->tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(d->tree, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QTreeWidgetItem *item = d->tree->itemAt(pos);
        if (!item || !(item->flags() & Qt::ItemIsSelectable))
            return;
        if (!item->isSelected()) {
            d->quiet = true;
            d->tree->clearSelection();
            item->setSelected(true);
            d->quiet = false;
            d->announce();
        }
        emit menuRequested(d->tree->viewport()->mapToGlobal(pos));
    });
}

AlbumTracksView::~AlbumTracksView()
{
    // The tree is destroyed after this body: it must not reach `d` any more.
    QObject::disconnect(d->tree, nullptr, this, nullptr);
    delete d;
}

void AlbumTracksView::setTracks(const QList<LibraryTrack> &tracksInOrder)
{
    // The tracks that are still there stay selected; listeners hear of it
    // only when the answer of selectedTrackIds() is another one.
    const QList<qint64> keep = d->selectedIds();
    d->quiet = true;
    d->fill(tracksInOrder);
    ++d->fills;
    // A row that was taken out with its album takes its selection with it.
    if (d->selectedIds() != keep)
        d->selectRowsOf(keep);
    // AV-VIEW: the spans were set while the headings were collapsed, which
    // capped the vertical scroll value; a fill goes back to the top.
    d->tree->scrollToTop();
    d->quiet = false;
    d->announce();
    d->askSoon(); // AV-COVER: at the next turn of the event loop, not here
}

void AlbumTracksView::setCoverCache(CoverCache *cache)
{
    if (d->covers.data() == cache)
        return;
    if (d->covers)
        QObject::disconnect(d->covers.data(), nullptr, this, nullptr);
    d->covers = cache;
    if (!cache) {
        d->tree->setIconSize(QSize());
        return;
    }
    d->tree->setIconSize(QSize(cache->edge(), cache->edge()));
    // The headings that are there are made as high as a cover (a one-off walk).
    QFont headingFont = d->tree->font();
    headingFont.setBold(true);
    const int height = d->headingHeight(headingFont);
    for (int g = 0; g < d->tree->topLevelItemCount(); ++g)
        d->tree->topLevelItem(g)->setSizeHint(0, QSize(0, height));
    // AV-COVER: a file the cache has now kept: the headings are looked at again.
    connect(cache, &CoverCache::loaded, this, [this]() { d->askSoon(); });
    d->askSoon();
}

CoverCache *AlbumTracksView::coverCache() const
{
    return d->covers.data();
}

QList<AlbumGroup> AlbumTracksView::groups() const
{
    return d->groups;
}

int AlbumTracksView::albumsBuilt() const
{
    return d->built;
}

int AlbumTracksView::fills() const
{
    return d->fills;
}

QList<qint64> AlbumTracksView::selectedTrackIds() const
{
    return d->selectedIds();
}

void AlbumTracksView::selectTracks(const QList<qint64> &ids)
{
    d->quiet = true;
    d->selectRowsOf(ids);
    d->quiet = false;
    d->announce();
}

void AlbumTracksView::jumpTo(QChar letter)
{
    const int at = jumpIndex(d->groups, letter);
    QTreeWidgetItem *heading = at < 0 ? nullptr : d->tree->topLevelItem(at);
    if (!heading)
        return; // no such album: nothing happens
    // The heading becomes the current row; the selection is left as it is.
    d->tree->setCurrentItem(heading, 0, QItemSelectionModel::NoUpdate);
    d->tree->scrollToItem(heading, QAbstractItemView::PositionAtTop);
}

int AlbumTracksView::currentGroup() const
{
    const QTreeWidgetItem *row = d->tree->currentItem();
    if (!row)
        return -1;
    // A track or disc row is a child of its heading, which carries the position.
    const QTreeWidgetItem *heading = row->parent() ? row->parent() : row;
    return d->tree->indexOfTopLevelItem(const_cast<QTreeWidgetItem *>(heading));
}

}
