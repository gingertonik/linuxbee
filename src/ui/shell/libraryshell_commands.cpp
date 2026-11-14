#include "shell/libraryshell.h"
#include "shell/libraryshell_p.h"

#include "actions/actionregistry.h"
#include "mainwindow/mainwindow.h"
#include "navigator/navigatorpanel.h"
#include "tracklist/tracklistmodel.h"
#include "tracklist/tracklistpage.h"

#include <QAction>
#include <QLineEdit>
#include <QStatusBar>

// Plan 30, task 2: SH-COMMANDS registers every command now; the behaviour of
// each lands in its own task. Contract: docs/spec/library-shell.md.
namespace linuxbee::ui {

void LibraryShell::registerShellCommands()
{
    // SH-COMMANDS: the shell is the context of every command, so destroying
    // the shell frees them (SH-BUILD).
    d->actions->implement(QStringLiteral("file.scanFolders"), this, [this] {
        // Task 4.
    });
    d->actions->implement(QStringLiteral("file.addFiles"), this, [this] {
        // Task 4.
    });
    d->actions->implement(QStringLiteral("file.remove"), this, [this] {
        // Task 5.
    });
    d->actions->implement(QStringLiteral("track.sendToInbox"), this, [this] {
        // Task 5.
    });
    d->actions->implement(QStringLiteral("track.sendToMusic"), this, [this] {
        // Task 5.
    });
    d->actions->implement(QStringLiteral("track.sendToAudiobooks"), this, [this] {
        // Task 5.
    });
    d->actions->implement(QStringLiteral("view.refresh"), this, [this] {
        // Task 7.
    });
    // T-PAGE: the track list page implements edit.find itself (focus with
    // the text selected); the registry keeps the first implementation, and
    // it is freed with the page, which the window owns.
    d->actions->implement(QStringLiteral("edit.find"), this, [this] {
        // Task 7: only used when no page answered the command first.
        if (QWidget *box = d->page->findChild<QLineEdit *>(QStringLiteral("searchBox")))
            box->setFocus();
    });
}

}
