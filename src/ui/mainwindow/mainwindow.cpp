#include "mainwindow/mainwindow.h"

// Stub. Plan 10 replaces every body in this file.
namespace linuxbee::ui {

struct MainWindow::Private { };

MainWindow::MainWindow(ActionRegistry *actions, QSettings *settings, QWidget *parent)
    : QMainWindow(parent)
    , d(nullptr)
{
    Q_UNUSED(actions);
    Q_UNUSED(settings);
}

MainWindow::~MainWindow()
{
    Q_UNUSED(d);
}

void MainWindow::setPanelWidget(Panel panel, QWidget *widget)
{
    Q_UNUSED(panel);
    Q_UNUSED(widget);
}

QWidget *MainWindow::panelWidget(Panel panel) const
{
    Q_UNUSED(panel);
    return nullptr;
}

bool MainWindow::addPage(const QString &pageId, QWidget *page)
{
    Q_UNUSED(pageId);
    Q_UNUSED(page);
    return false;
}

bool MainWindow::showPage(const QString &pageId)
{
    Q_UNUSED(pageId);
    return false;
}

QString MainWindow::currentPage() const
{
    return { };
}

QStringList MainWindow::pages() const
{
    return { };
}

void MainWindow::saveLayout()
{
}

void MainWindow::restoreLayout()
{
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QMainWindow::closeEvent(event);
}

}
