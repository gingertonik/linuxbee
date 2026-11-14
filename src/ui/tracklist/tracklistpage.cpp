#include "tracklist/tracklistpage.h"

#include "actions/actionregistry.h"
#include "search/search.h"
#include "tracklist/tracklistmodel.h"

#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
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

    connect(d->search, &QLineEdit::textChanged, this,
        [this](const QString &text) { d->model->setFilter(SearchQuery::parse(text)); });
    connect(d->view->header(), &QHeaderView::sortIndicatorChanged, this,
        [this](int column, Qt::SortOrder order) { d->model->sort(column, order); });
    connect(d->view, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        d->contextMenu->popup(d->view->viewport()->mapToGlobal(pos));
    });
    connect(model, &TrackListModel::summaryChanged, this,
        [this]() { d->summary->setText(d->model->summary()); });
}

TrackListPage::~TrackListPage()
{
    delete d;
}

QList<qint64> TrackListPage::selectedTrackIds() const
{
    return { };
}

void TrackListPage::selectTracks(const QList<qint64> &ids)
{
    Q_UNUSED(ids);
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
