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

// Plan 30: SH-COMMANDS registers every command in task 2; each behaviour
// lands in its own task. Contract: docs/spec/library-shell.md.
namespace linuxbee::ui {

void LibraryShell::registerShellCommands()
{
    // SH-COMMANDS: the shell is the context of every command, so destroying
    // the shell frees them (SH-BUILD).
    d->actions->implement(QStringLiteral("file.scanFolders"), this, [this] { scanFolders(); });
    d->actions->implement(QStringLiteral("file.addFiles"), this, [this] { addFiles(); });
    d->actions->implement(QStringLiteral("file.remove"), this, [this] { removeSelected(); });
    d->actions->implement(QStringLiteral("track.sendToInbox"), this,
        [this] { sendToSection(QStringLiteral("Inbox")); });
    d->actions->implement(QStringLiteral("track.sendToMusic"), this,
        [this] { sendToSection(QStringLiteral("Music")); });
    d->actions->implement(QStringLiteral("track.sendToAudiobooks"), this,
        [this] { sendToSection(QStringLiteral("Audiobooks")); });
    d->actions->implement(QStringLiteral("view.refresh"), this, [this] { refreshAll(); });
    // T-PAGE: the track list page implements edit.find itself (focus with
    // the text selected); the registry keeps the first implementation, and
    // it is freed with the page, which the window owns.
    d->actions->implement(QStringLiteral("edit.find"), this, [this] {
        // The fallback, used only when no page answered the command first.
        if (QWidget *box = d->page->findChild<QLineEdit *>(QStringLiteral("searchBox")))
            box->setFocus();
    });
}

QString LibraryShell::targetSection() const
{
    // SH-COMMANDS: new files go to the section on show; Music when the node
    // is not a library section.
    if (const QString section = sectionOf(d->node); !section.isEmpty())
        return section;
    return kMusicSection;
}

void LibraryShell::showMessage(const QString &text)
{
    // SH-MESSAGE: one line in the window's status bar, kept as lastMessage().
    d->message = text;
    d->window->statusBar()->showMessage(text);
}

void LibraryShell::scanFolders()
{
    // SH-COMMANDS: a chooser that answers nothing ends the command, quietly.
    if (!d->hooks.chooseFolders || !d->hooks.now)
        return;
    const QStringList folders = d->hooks.chooseFolders();
    if (folders.isEmpty())
        return;
    ScanOptions options;
    options.category = targetSection();
    options.now = d->hooks.now();
    const ScanReport report = d->library->scan(folders, options);
    showMessage(tr("Scan: %1 added, %2 updated, %3 missing, %4 not audio")
            .arg(report.added.size())
            .arg(report.updated.size())
            .arg(report.nowMissing.size())
            .arg(report.notAudio.size()));
}

void LibraryShell::addFiles()
{
    // SH-COMMANDS: Add Files, the same section rule.
    if (!d->hooks.chooseFiles || !d->hooks.now)
        return;
    const QStringList files = d->hooks.chooseFiles();
    if (files.isEmpty())
        return;
    const QString section = targetSection();
    const QDateTime now = d->hooks.now();
    int added = 0;
    for (const QString &path : files) {
        if (d->library->addFile(path, section, now) != 0)
            ++added;
    }
    showMessage(tr("Added %1 of %2 files").arg(added).arg(files.size()));
}

void LibraryShell::removeSelected()
{
    // SH-COMMANDS: Remove from Library. The three commands on the selection
    // do nothing, without a message, when nothing is selected. No file is
    // deleted from the disk.
    const QList<qint64> ids = d->page->selectedTrackIds();
    if (ids.isEmpty())
        return;
    const int removed = d->library->remove(ids);
    showMessage(tr("Removed %1 from the library").arg(removed));
}

void LibraryShell::sendToSection(const QString &section)
{
    // SH-COMMANDS: Send To puts the selected tracks in the section.
    const QList<qint64> ids = d->page->selectedTrackIds();
    if (ids.isEmpty())
        return;
    const int moved = d->library->setCategory(ids, section);
    showMessage(tr("Moved %1 to %2").arg(moved).arg(section));
}

void LibraryShell::refreshAll()
{
    // SH-COMMANDS: view.refresh reads counts, playlists and the node's
    // tracks again. The search text is the user's; it stays.
    refreshCounts();
    d->navigator->setPlaylists(playlistIds());
    showNode();
}

}
