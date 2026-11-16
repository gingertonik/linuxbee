#include "shell/libraryshell.h"
#include "shell/libraryshell_p.h"

#include "actions/actionregistry.h"
#include "files/trash.h"
#include "mainwindow/mainwindow.h"
#include "navigator/navigatorpanel.h"
#include "shell/scanrunner.h"
#include "tracklist/tracklistmodel.h"
#include "tracklist/tracklistpage.h"

#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QLineEdit>
#include <QMessageBox>
#include <QSet>
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
    d->actions->implement(QStringLiteral("file.delete"), this, [this] { deleteSelected(); });
    d->actions->implement(QStringLiteral("track.sendToInbox"), this,
        [this] { sendToSection(QStringLiteral("Inbox")); });
    d->actions->implement(QStringLiteral("track.sendToMusic"), this,
        [this] { sendToSection(QStringLiteral("Music")); });
    d->actions->implement(QStringLiteral("track.sendToAudiobooks"), this,
        [this] { sendToSection(QStringLiteral("Audiobooks")); });
    // SH-COMMANDS: view.refresh — counts, playlists and the node's tracks
    // are read again (refreshAll); the search text is the user's and stays,
    // because showNode() never touches the box. Task 7's gate is
    // `searchAndFind stored`.
    d->actions->implement(QStringLiteral("view.refresh"), this, [this] { refreshAll(); });
    // T-PAGE: the track list page implements edit.find itself (focus with
    // the text selected); the registry keeps the first implementation, and
    // it is freed with the page, which the window owns. SH-COMMANDS:
    // edit.find puts the keyboard focus in the list's search box.
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
    // SR-SHELL: the command comes back at once; the runner's signals say the rest.
    d->scanner->scan(folders, options);
    showMessage(tr("Scan started"));
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

    // SH-REMOVE: in a playlist the tracks leave the playlist, in one change, and nothing is
    // asked. The library and the files are not touched.
    if (const QString playlist = playlistOf(d->node); !playlist.isEmpty()) {
        QSet<QString> selected;
        for (const qint64 id : ids) {
            if (const std::optional<LibraryTrack> track = d->library->track(id))
                selected.insert(track->path);
        }
        const QStringList entries = d->playlists->tracks(playlist);
        QList<int> positions;
        for (int i = 0; i < entries.size(); ++i) {
            if (selected.contains(entries.at(i)))
                positions.append(i);
        }
        if (positions.isEmpty())
            return;
        d->playlists->removeAt(playlist, positions);
        const qsizetype n = selected.size();
        showMessage(n == 1 ? tr("Removed 1 track from the playlist")
                           : tr("Removed %1 tracks from the playlist").arg(n));
        return;
    }

    // SH-REMOVE: anywhere else it asks first; No is the default and ends the command.
    const QString question = ids.size() == 1
        ? tr("Remove 1 track from the library? The file stays. Its play count and date added are "
             "lost.")
        : tr("Remove %1 tracks from the library? The files stay. Their play counts and dates added "
             "are lost.")
              .arg(ids.size());
    const bool yes = d->hooks.confirm
        ? d->hooks.confirm(question)
        : QMessageBox::question(d->window, tr("Remove from Library"), question,
              QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            == QMessageBox::Yes;
    if (!yes)
        return;
    const int removed = d->library->remove(ids);
    showMessage(tr("Removed %1 from the library").arg(removed));
}

void LibraryShell::deleteSelected()
{
    // SH-DELETE: Delete Files. Nothing is selected: nothing is asked and nothing is said.
    const QList<qint64> ids = d->page->selectedTrackIds();
    if (ids.isEmpty())
        return;
    // The tracks' paths, in the order shown. Nothing on disk or in the library is touched until
    // the question is answered.
    QStringList paths;
    QHash<QString, qint64> idOfPath; // clean absolute path, as moveFilesToTrash gives it back
    QHash<qint64, QString> pathOfId; // the path as the library held it
    for (const qint64 id : ids) {
        const std::optional<LibraryTrack> track = d->library->track(id);
        if (!track)
            continue;
        paths.append(track->path);
        pathOfId.insert(id, track->path);
        idOfPath.insert(QDir::cleanPath(QFileInfo(track->path).absoluteFilePath()), id);
    }
    if (paths.isEmpty())
        return;

    // Step 1: the question. No is the default of the dialog, and ends the command: no file,
    // no track, no message, and the selection stays as it was.
    const QString question = paths.size() == 1
        ? tr("Move 1 file to the trash?")
        : tr("Move %1 files to the trash?").arg(paths.size());
    const bool yes = d->hooks.confirm
        ? d->hooks.confirm(question)
        : QMessageBox::question(d->window, tr("Delete Files"), question,
              QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            == QMessageBox::Yes;
    if (!yes)
        return;

    // Steps 2 and 3: to the trash, or the file stays. Only the tracks whose files went leave the
    // library, in one call. No file is removed in any other way.
    int notThere = 0;
    int couldNot = 0; // refused and failed together
    QList<qint64> gone;
    for (const TrashOutcome &outcome : moveFilesToTrash(paths)) {
        switch (outcome.status) {
        case TrashOutcome::Status::Trashed:
            if (idOfPath.contains(outcome.path))
                gone.append(idOfPath.value(outcome.path));
            break;
        case TrashOutcome::Status::NotThere:
            ++notThere;
            break;
        case TrashOutcome::Status::Refused:
        case TrashOutcome::Status::Failed:
            ++couldNot;
            break;
        }
    }
    if (!gone.isEmpty()) {
        d->library->remove(gone);
        // Step 5: once, after the library was told: the paths as the library held them, in the
        // order shown.
        const QSet<qint64> wentIds(gone.cbegin(), gone.cend());
        QStringList wentPaths;
        for (const qint64 id : ids) {
            if (wentIds.contains(id) && pathOfId.contains(id))
                wentPaths.append(pathOfId.value(id));
        }
        emit filesTrashed(wentPaths);
    }

    // Step 4: the message.
    QString text;
    if (gone.isEmpty())
        text = tr("No file was moved to the trash");
    else if (gone.size() == 1)
        text = tr("Moved 1 file to the trash");
    else
        text = tr("Moved %1 files to the trash").arg(gone.size());
    if (notThere == 1)
        text += tr("; 1 was not there");
    else if (notThere > 1)
        text += tr("; %1 were not there").arg(notThere);
    if (couldNot > 0)
        text += tr("; %1 could not be moved").arg(couldNot);
    showMessage(text);
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
    d->switchOrder(); // SH-NODE: view.refresh shows the node's own order again
    showNode();
}

}
