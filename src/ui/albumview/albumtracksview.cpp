#include "albumview/albumtracksview.h"

#include <QFileInfo>
#include <QHash>
#include <QHeaderView>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QSet>
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

}

struct AlbumTracksView::Private {
    AlbumTracksView *q = nullptr;
    QTreeWidget *tree = nullptr;
    QList<AlbumGroup> groups;
    QHash<qint64, QTreeWidgetItem *> trackRows; // rebuilt with the tree
    QList<qint64> announced; // selectedTrackIds() as the listeners last heard it
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
        for (int g = 0; g < groups.size(); ++g) {
            const AlbumGroup &group = groups.at(g);
            auto *heading = new QTreeWidgetItem(tree);
            heading->setText(0, albumHeading(group) + QLatin1Char('\n') + albumDetail(group));
            heading->setFont(0, headingFont);
            heading->setData(0, ValueRole, g);

            // The rows of the album are made apart and handed over in one call.
            QList<QTreeWidgetItem *> children;
            QList<QTreeWidgetItem *> discRows;
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
                    discRows.append(disc);
                    ++nextDisc;
                }
                QTreeWidgetItem *row = trackRow(track);
                trackRows.insert(track.id, row);
                children.append(row);
            }
            heading->addChildren(children);

            // Spanning works only for items that are in the tree.
            heading->setFirstColumnSpanned(true);
            for (QTreeWidgetItem *disc : discRows)
                disc->setFirstColumnSpanned(true);
            heading->setExpanded(true);
            first += group.trackIds.size();
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
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(1, QHeaderView::Stretch);
    header->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(d->tree, 1);

    connect(d->tree, &QTreeWidget::itemSelectionChanged, this, [this]() {
        if (!d->quiet)
            d->announce();
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
    d->selectRowsOf(keep);
    d->quiet = false;
    d->announce();
}

QList<AlbumGroup> AlbumTracksView::groups() const
{
    return d->groups;
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
    Q_UNUSED(letter);
}

int AlbumTracksView::currentGroup() const
{
    return -2;
}

}
