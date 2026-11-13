#include "mainwindow/mainwindow.h"
#include "actions/actionregistry.h"
#include "app/appinfo.h"

#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

#include <QHash>
#include <QStringList>

// Plan 10: the frame every other part is put into. Contract: docs/spec/main-window.md
// (W-FRAME, W-PANELS, W-PAGES). The window's own commands and its remembered
// layout live in mainwindow_commands.cpp.
namespace linuxbee::ui {

struct MainWindow::Private {
    ActionRegistry *actions = nullptr;
    QSettings *settings = nullptr;
    QSplitter *splitter = nullptr;
    QStackedWidget *stack = nullptr;
    QWidget *navigator = nullptr;
    QWidget *right = nullptr;
    QWidget *playerBar = nullptr;
    QHash<QWidget *, QString> pageIds; // pages carry their id; the stack holds their order
    QStringList order; // page ids, in the order added
    QHash<Panel, QWidget *> contents; // W-PANELS: the widget filling each panel, if any
};

MainWindow::MainWindow(ActionRegistry *actions, QSettings *settings, QWidget *parent)
    : QMainWindow(parent)
    , d(new Private)
{
    d->actions = actions;
    d->settings = settings;

    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(applicationDisplayName());
    setMinimumSize(640, 400); // W-FRAME

    // W-FRAME: three panels side by side, the player bar below them, full width.
    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("centralArea"));
    auto *frame = new QVBoxLayout(central);
    frame->setContentsMargins(0, 0, 0, 0);
    frame->setSpacing(0);

    d->splitter = new QSplitter(Qt::Horizontal, central);
    d->splitter->setObjectName(QStringLiteral("mainSplitter"));
    d->splitter->setChildrenCollapsible(false);

    d->navigator = new QWidget(d->splitter);
    d->navigator->setObjectName(QStringLiteral("navigatorPanel"));
    d->navigator->setMinimumWidth(120);
    auto *navLayout = new QHBoxLayout(d->navigator);
    navLayout->setContentsMargins(0, 0, 0, 0);

    d->stack = new QStackedWidget(d->splitter);
    d->stack->setObjectName(QStringLiteral("mainPanel"));
    d->stack->setMinimumWidth(200);

    d->right = new QWidget(d->splitter);
    d->right->setObjectName(QStringLiteral("rightPanel"));
    d->right->setMinimumWidth(120);
    auto *rightLayout = new QHBoxLayout(d->right);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    d->splitter->addWidget(d->navigator);
    d->splitter->addWidget(d->stack);
    d->splitter->addWidget(d->right);
    // W-LAYOUT's first-run widths: the main panel takes the room the others do not use.
    d->splitter->setStretchFactor(0, 0);
    d->splitter->setStretchFactor(1, 1);
    d->splitter->setStretchFactor(2, 0);
    d->splitter->setSizes({ 220, -1, 300 });

    d->playerBar = new QWidget(central);
    d->playerBar->setObjectName(QStringLiteral("playerBar"));
    d->playerBar->setFixedHeight(48); // fixed height; its content is a later plan
    auto *barLayout = new QHBoxLayout(d->playerBar);
    barLayout->setContentsMargins(0, 0, 0, 0);

    frame->addWidget(d->splitter);
    frame->addWidget(d->playerBar);
    setCentralWidget(central);

    statusBar()->setObjectName(QStringLiteral("statusBar"));

    // W-FRAME: the menu bar is the registry's, and the window listens for
    // every command's hotkey, whichever part has the focus.
    if (d->actions) {
        d->actions->populateMenuBar(menuBar());
        for (const QString &id : d->actions->ids())
            addAction(d->actions->action(id));
    }
}

MainWindow::~MainWindow()
{
    delete d;
}

// The layout inside a panel; the panel keeps its object name, its content fills it.
static QHBoxLayout *panelLayout(QWidget *panel)
{
    auto *layout = qobject_cast<QHBoxLayout *>(panel->layout());
    return layout;
}

void MainWindow::setPanelWidget(Panel panel, QWidget *widget)
{
    // W-PANELS: replace what is there (the old widget is deleted); null empties the panel.
    QWidget *container = panel == Panel::Navigator
        ? d->navigator
        : (panel == Panel::Right ? d->right : d->playerBar);
    QHBoxLayout *layout = panelLayout(container);
    if (QWidget *old = d->contents.take(panel)) {
        layout->removeWidget(old);
        old->deleteLater(); // the window owned it; the replacement deletes it
    }
    if (widget) {
        widget->setParent(container);
        layout->addWidget(widget);
        d->contents.insert(panel, widget);
    }
}

QWidget *MainWindow::panelWidget(Panel panel) const
{
    return d->contents.value(panel, nullptr);
}

bool MainWindow::addPage(const QString &pageId, QWidget *page)
{
    // W-PAGES
    if (pageId.isEmpty() || !page || d->pageIds.contains(page))
        return false;
    if (d->order.contains(pageId))
        return false; // the id is taken
    d->stack->addWidget(page);
    d->pageIds.insert(page, pageId);
    d->order.append(pageId);
    if (d->order.size() == 1)
        emit currentPageChanged(pageId); // the first page added is shown
    return true;
}

bool MainWindow::showPage(const QString &pageId)
{
    const int index = d->order.indexOf(pageId);
    if (index < 0)
        return false;
    if (d->stack->currentIndex() == index)
        return true; // already shown: fine, and no signal
    d->stack->setCurrentIndex(index);
    emit currentPageChanged(pageId);
    return true;
}

QString MainWindow::currentPage() const
{
    QWidget *page = d->stack->currentWidget();
    return page ? d->pageIds.value(page) : QString();
}

QStringList MainWindow::pages() const
{
    return d->order;
}

void MainWindow::saveLayout()
{
    // W-LAYOUT. Implemented in mainwindow_commands.cpp (task 5).
}

void MainWindow::restoreLayout()
{
    // W-LAYOUT. Implemented in mainwindow_commands.cpp (task 5).
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QMainWindow::closeEvent(event);
}

} // namespace linuxbee::ui
