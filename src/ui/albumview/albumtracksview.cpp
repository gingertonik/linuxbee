#include "albumview/albumtracksview.h"

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

// What a row carries in this role: a heading, the position of its group; a
// track row, the id of its track. A disc row carries nothing.
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

}

struct AlbumTracksView::Private {
    AlbumTracksView *q = nullptr;
    QWidget *bar = nullptr; // jumpBar
    QTreeWidget *tree = nullptr;
    QList<AlbumGroup> groups;
    QHash<qint64, QTreeWidgetItem *> trackRows; // rebuilt with the tree
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
        QSet<int> headings;
        QSet<qint64> tracks;
        for (const QTreeWidgetItem *item : tree->selectedItems()) {
            if (!item->parent())
                headings.insert(item->data(0, ValueRole).toInt());
            else
                tracks.insert(item->data(0, ValueRole).toLongLong());
        }
        QList<qint64> ids;
        if (headings.isEmpty() && tracks.isEmpty())
            return ids;
        QSet<qint64> seen;
        for (int g = 0; g < groups.size(); ++g) {
            const bool whole = headings.contains(g);
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
            emit q->albumActivated(item->data(0, ValueRole).toInt());
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

    // AV-VIEW: the tree, from the groups of these tracks.
    void fill(const QList<LibraryTrack> &tracks)
    {
        tree->clear();
        trackRows.clear();
        groups = albumGroups(tracks);

        QFont headingFont = tree->font();
        headingFont.setBold(true);
        qsizetype first = 0; // where the group's first track stands in `tracks`
        QList<QTreeWidgetItem *> spanned; // the headings and disc rows
        for (int g = 0; g < groups.size(); ++g) {
            const AlbumGroup &group = groups.at(g);
            auto *heading = new QTreeWidgetItem(tree);
            ++built;
            heading->setText(0, albumHeading(group) + QLatin1Char('\n') + albumDetail(group));
            heading->setFont(0, headingFont);
            heading->setData(0, ValueRole, g);

            // The rows of the album are made apart and handed over in one call.
            QList<QTreeWidgetItem *> children;
            spanned.append(heading);
            int nextDisc = 0;
            for (int i = 0; i < group.trackIds.size(); ++i) {
                const LibraryTrack &track = tracks.at(first + i);
                if (nextDisc < group.discStarts.size() && group.discStarts.at(nextDisc) == i) {
                    auto *disc = new QTreeWidgetItem;
                    disc->setText(0,
                        AlbumTracksView::tr("Disc %1").arg(
                            firstValue(track, QStringLiteral("Disc#"))));
                    disc->setFlags(Qt::ItemIsEnabled); // it cannot be selected
                    children.append(disc);
                    spanned.append(disc);
                    ++nextDisc;
                }
                QTreeWidgetItem *row = trackRow(track);
                trackRows.insert(track.id, row);
                children.append(row);
            }
            heading->addChildren(children);
            first += group.trackIds.size();
        }
        // AV-VIEW, Speed: every heading and disc row is made to span the
        // columns in one pass, once the tree is built (spanning works only for
        // items that are in the tree), and then every heading is expanded in
        // one step, not one by one as each is added.
        for (QTreeWidgetItem *item : spanned)
            item->setFirstColumnSpanned(true);
        tree->expandAll();
        // AV-VIEW: the spans were set while the headings were collapsed, which
        // capped the vertical scroll value; a fill goes back to the top.
        tree->scrollToTop();
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
    d->selectRowsOf(keep);
    d->quiet = false;
    d->announce();
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
    return heading->data(0, ValueRole).toInt();
}

}
