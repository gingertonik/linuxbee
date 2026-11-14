#include "tracklist/tracklistpage.h"

#include "actions/actionregistry.h"
#include "search/search.h"
#include "tracklist/tracklistmodel.h"

#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSet>
#include <QTreeView>
#include <QVBoxLayout>

// Contract: docs/spec/track-list.md (T-PAGE).
namespace linuxbee::ui {

struct TrackListPage::Private {
    TrackListModel *model = nullptr;
    ActionRegistry *actions = nullptr;
    QLineEdit *search = nullptr;
    QTreeView *view = nullptr;
    QLabel *summary = nullptr;
    QMenu *contextMenu = nullptr;
    // The selection belongs to the tracks. The view's own selection is
    // derived from this set, and is cleared by the view on every reset.
    QSet<qint64> selectedIds;
    // True while the page itself moves the view's selection, so the view's
    // signal is not read back as the user's choice.
    bool quiet = false;

    QSet<qint64> idsOfViewSelection() const
    {
        QSet<qint64> ids;
        const QModelIndexList rows = view->selectionModel()->selectedRows();
        for (const QModelIndex &index : rows)
            ids.insert(model->trackId(index.row()));
        return ids;
    }

    void applySelection(const QSet<qint64> &ids)
    {
        quiet = true;
        // One selection of row ranges, handed over in one call: a call per
        // row costs time in proportion to the rows already selected.
        QItemSelection selection;
        const int rows = model->rowCount();
        int first = -1;
        for (int row = 0; row <= rows; ++row) {
            const bool wanted = row < rows && ids.contains(model->trackId(row));
            if (wanted && first < 0)
                first = row;
            if (!wanted && first >= 0) {
                selection.select(model->index(first, 0), model->index(row - 1, 0));
                first = -1;
            }
        }
        view->selectionModel()->select(
            selection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        quiet = false;
    }
};

TrackListPage::TrackListPage(TrackListModel *model, ActionRegistry *actions, QWidget *parent)
    : QWidget(parent)
    , d(new Private)
{
    d->model = model;
    d->actions = actions;
    setObjectName(QStringLiteral("trackListPage"));

    d->search = new QLineEdit(this);
    d->search->setObjectName(QStringLiteral("searchBox"));
    d->search->setPlaceholderText(tr("Search"));
    d->search->setClearButtonEnabled(true);

    d->view = new QTreeView(this);
    d->view->setObjectName(QStringLiteral("trackView"));
    d->view->setModel(model);
    d->view->setRootIsDecorated(false);
    d->view->setSelectionBehavior(QAbstractItemView::SelectRows);
    d->view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    d->view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    d->view->setUniformRowHeights(true);
    d->view->header()->setSectionsMovable(true);
    d->view->header()->setSortIndicatorShown(true);

    d->summary = new QLabel(this);
    d->summary->setObjectName(QStringLiteral("summaryLabel"));
    d->summary->setText(model->summary());

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(d->search);
    layout->addWidget(d->view, 1);
    layout->addWidget(d->summary);

    d->contextMenu = new QMenu(this);
    d->contextMenu->setObjectName(QStringLiteral("trackContextMenu"));
    if (actions)
        actions->populateMenu(d->contextMenu, QStringLiteral("Context"));
    d->view->setContextMenuPolicy(Qt::CustomContextMenu);

    // Makes the given tracks the selection, as far as they are shown. Emits
    // selectionChanged only when the set of tracks changed.
    auto chooseTracks = [this](const QSet<qint64> &wanted) {
        QSet<qint64> next;
        for (int row = 0; row < d->model->rowCount(); ++row) {
            const qint64 id = d->model->trackId(row);
            if (wanted.contains(id))
                next.insert(id);
        }
        d->applySelection(next);
        if (next != d->selectedIds) {
            d->selectedIds = next;
            emit selectionChanged();
        }
    };

    connect(d->search, &QLineEdit::textChanged, this,
        [this](const QString &text) { d->model->setFilter(SearchQuery::parse(text)); });
    connect(d->view->header(), &QHeaderView::sortIndicatorChanged, this,
        [this](int column, Qt::SortOrder order) { d->model->sort(column, order); });
    connect(d->view, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        d->contextMenu->popup(d->view->viewport()->mapToGlobal(pos));
    });
    connect(model, &TrackListModel::summaryChanged, this,
        [this]() { d->summary->setText(d->model->summary()); });

    connect(d->view->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this]() {
        if (d->quiet)
            return;
        const QSet<qint64> next = d->idsOfViewSelection();
        if (next != d->selectedIds) {
            d->selectedIds = next;
            emit selectionChanged();
        }
    });

    // The view forgets its selection on a model reset: put back the tracks
    // that are still shown, and tell only if the set of tracks changed.
    connect(model, &QAbstractItemModel::modelAboutToBeReset, this, [this]() { d->quiet = true; });
    connect(model, &QAbstractItemModel::modelReset, this, [this]() {
        const QSet<qint64> before = d->selectedIds;
        QSet<qint64> after;
        for (const qint64 id : before) {
            if (d->model->rowOf(id) >= 0)
                after.insert(id);
        }
        d->selectedIds = after;
        d->applySelection(after);
        d->quiet = false;
        if (after != before)
            emit selectionChanged();
    });

    connect(d->view, &QAbstractItemView::activated, this, [this](const QModelIndex &index) {
        if (index.isValid())
            emit trackActivated(d->model->trackId(index.row()));
    });

    if (actions) {
        actions->implement(QStringLiteral("edit.selectAll"), this, [this, chooseTracks]() {
            QSet<qint64> all;
            for (int row = 0; row < d->model->rowCount(); ++row)
                all.insert(d->model->trackId(row));
            chooseTracks(all);
        });
        actions->implement(QStringLiteral("edit.find"), this, [this]() {
            d->search->setFocus();
            d->search->selectAll();
        });
    }
}

TrackListPage::~TrackListPage()
{
    delete d;
}

QList<qint64> TrackListPage::selectedTrackIds() const
{
    QList<qint64> ids;
    for (int row = 0; row < d->model->rowCount(); ++row) {
        const qint64 id = d->model->trackId(row);
        if (d->selectedIds.contains(id))
            ids.append(id);
    }
    return ids;
}

void TrackListPage::selectTracks(const QList<qint64> &ids)
{
    QSet<qint64> wanted(ids.begin(), ids.end());
    QSet<qint64> next;
    for (int row = 0; row < d->model->rowCount(); ++row) {
        const qint64 id = d->model->trackId(row);
        if (wanted.contains(id))
            next.insert(id);
    }
    d->applySelection(next);
    if (next != d->selectedIds) {
        d->selectedIds = next;
        emit selectionChanged();
    }
}

QString TrackListPage::searchText() const
{
    return d->search->text();
}

void TrackListPage::setSearchText(const QString &text)
{
    d->search->setText(text);
}

}
