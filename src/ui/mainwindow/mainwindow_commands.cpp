#include "mainwindow/mainwindow.h"
#include "actions/actionregistry.h"
#include "app/appinfo.h"
#include "mainwindow_p.h"

#include <QAction>
#include <QCloseEvent>
#include <QMessageBox>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>

// W-COMMANDS and W-LAYOUT. Contract: docs/spec/main-window.md.
// The window implements six commands itself and remembers its size, the
// three panel widths and which parts are shown, each stored value used
// only if it makes sense on its own.
namespace linuxbee::ui {

namespace {
constexpr int kMaxSize = 16384; // W-LAYOUT: a stored value above this is nonsense
}

void MainWindow::captureWidths()
{
    const QList<int> sizes = d->splitter->sizes();
    for (int i = 0; i < 3 && i < sizes.size(); ++i) {
        if (sizes.at(i) > 0)
            d->widths[i] = sizes.at(i); // a hidden panel reports 0; keep the width it had
    }
}

void MainWindow::setPanelVisibility(int panelIndex, bool shown)
{
    captureWidths();
    QWidget *panel = d->splitter->widget(panelIndex);
    panel->setVisible(shown);
    if (panelIndex == 0)
        d->navigatorShown = shown;
    else if (panelIndex == 1)
        d->rightShown = shown;
}

void MainWindow::applyPanelVisibility()
{
    captureWidths();
    d->splitter->widget(0)->setVisible(d->navigatorShown);
    d->splitter->widget(2)->setVisible(d->rightShown);
    statusBar()->setVisible(d->statusBarShown);
    if (d->actions) {
        if (QAction *a = d->actions->action(QStringLiteral("view.navigator")))
            a->setChecked(d->navigatorShown);
        if (QAction *a = d->actions->action(QStringLiteral("view.rightPanel")))
            a->setChecked(d->rightShown);
        if (QAction *a = d->actions->action(QStringLiteral("view.statusBar")))
            a->setChecked(d->statusBarShown);
    }
}

void MainWindow::implementWindowCommands()
{
    if (!d->actions)
        return;
    ActionRegistry *reg = d->actions;

    // W-COMMANDS: show or hide a part; the tick shows the state. A tick-box
    // command's function reads the new state from the action (H-IMPLEMENT).
    reg->implement(QStringLiteral("view.navigator"), this, [this] {
        const auto *act = qobject_cast<const QAction *>(sender());
        const bool shown = act ? act->isChecked() : true;
        setPanelVisibility(0, shown);
        d->navigatorShown = shown;
    });
    reg->implement(QStringLiteral("view.rightPanel"), this, [this] {
        const auto *act = qobject_cast<const QAction *>(sender());
        const bool shown = act ? act->isChecked() : true;
        setPanelVisibility(2, shown);
        d->rightShown = shown;
    });
    reg->implement(QStringLiteral("view.statusBar"), this, [this, reg] {
        const auto *act = qobject_cast<const QAction *>(sender());
        const bool shown = act ? act->isChecked() : true;
        statusBar()->setVisible(shown);
        d->statusBarShown = shown;
        if (QAction *a = reg->action(QStringLiteral("view.statusBar")))
            a->setChecked(shown);
    });

    // Full screen and back. The tick shows the state; saveLayout keeps the
    // pre-full-screen size (normalGeometry), so it is never stored as the size.
    reg->implement(QStringLiteral("view.fullScreen"), this, [this, reg] {
        const bool now = !isFullScreen();
        if (now)
            captureWidths();
        setWindowState((windowState() & ~Qt::WindowFullScreen)
            | (now ? Qt::WindowFullScreen : Qt::WindowNoState));
        if (QAction *a = reg->action(QStringLiteral("view.fullScreen")))
            a->setChecked(now);
    });

    // Exit closes the window (and closing saves the layout).
    reg->implement(QStringLiteral("file.quit"), this, [this] { close(); });

    // About: a dialog that does not block, saying which version this is.
    reg->implement(QStringLiteral("help.about"), this, [this] {
        auto *box = new QMessageBox(QMessageBox::Information, tr("About linuxBee"), versionLine(),
            QMessageBox::StandardButton::Ok, this);
        box->setObjectName(QStringLiteral("aboutDialog"));
        box->open(); // shows without blocking the caller (exec() would hang a test)
    });

    // The tick of a panel command shows the state the layout restored.
    applyPanelVisibility();
}

void MainWindow::saveLayout()
{
    // W-LAYOUT: the group `window`; nothing outside it is touched.
    captureWidths();
    const QSize size = normalGeometry().size(); // in full screen: the size from before it
    d->settings->beginGroup(QStringLiteral("window"));
    d->settings->setValue(QStringLiteral("size"), size);
    d->settings->setValue(
        QStringLiteral("splitter"), QVariantList({ d->widths[0], d->widths[1], d->widths[2] }));
    d->settings->setValue(QStringLiteral("navigatorVisible"), d->navigatorShown);
    d->settings->setValue(QStringLiteral("rightPanelVisible"), d->rightShown);
    d->settings->setValue(QStringLiteral("statusBarVisible"), d->statusBarShown);
    d->settings->endGroup();
    d->settings->sync();
}

void MainWindow::restoreLayout()
{
    if (!d->settings)
        return;
    d->settings->beginGroup(QStringLiteral("window"));
    const QSize size = d->settings->value(QStringLiteral("size")).toSize();
    const QVariant splitter = d->settings->value(QStringLiteral("splitter"));
    const QVariant navigator = d->settings->value(QStringLiteral("navigatorVisible"));
    const QVariant right = d->settings->value(QStringLiteral("rightPanelVisible"));
    const QVariant status = d->settings->value(QStringLiteral("statusBarVisible"));
    d->settings->endGroup();

    // A file written by an earlier run hands every value back as text: accept the
    // plain bool and the words true/false; anything else means the default (shown).
    const auto flag = [](const QVariant &stored) {
        if (stored.typeId() == QMetaType::Bool)
            return stored.toBool();
        const QString text = stored.toString().trimmed();
        if (text.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0)
            return true;
        if (text.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0)
            return false;
        return true;
    };
    d->navigatorShown = flag(navigator);
    d->rightShown = flag(right);
    d->statusBarShown = flag(status);

    if (size.width() >= minimumWidth() && size.height() >= minimumHeight()
        && size.width() < kMaxSize && size.height() < kMaxSize)
        resize(size);

    // Three panel widths, as a list written by this program or as the text a file
    // gives back ("180, 500, 320" arrives as a QStringList of strings). Each entry
    // must be a whole number; one bad entry rejects the whole value.
    QList<int> sizes;
    bool sane = (splitter.typeId() == QMetaType::QVariantList
                    || splitter.typeId() == QMetaType::QStringList)
        && splitter.toList().size() == 3;
    if (sane) {
        for (const QVariant &v : splitter.toList()) {
            bool ok = false;
            const int width = v.toString().trimmed().toInt(&ok);
            if (!ok || width < 0 || width >= kMaxSize) {
                sane = false;
                break;
            }
            sizes.append(width);
        }
    }
    if (sane) {
        for (int i = 0; i < 3; ++i)
            d->widths[i] = sizes.at(i);
        d->splitter->setSizes(sizes);
    }
    applyPanelVisibility();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveLayout(); // W-LAYOUT: closing the window saves it
    QMainWindow::closeEvent(event);
}

} // namespace linuxbee::ui
