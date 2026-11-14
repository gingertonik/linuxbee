#include "shell/playlistediting.h"

#include <QAction>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QStatusBar>

#include <utility>

#include "actions/actionregistry.h"
#include "mainwindow/mainwindow.h"
#include "navigator/navigatorpanel.h"
#include "shell/libraryshell.h"

namespace linuxbee::ui {

namespace {

// The part of a row id after its prefix, or empty when the id has another prefix.
QString afterPrefix(const QString &rowId, const QString &prefix)
{
    if (!rowId.startsWith(prefix))
        return QString();
    return rowId.mid(prefix.size());
}

// The playlist id of a "playlist:" row: "Rock/Best of".
QString playlistIdOf(const QString &rowId)
{
    return afterPrefix(rowId, QStringLiteral("playlist:"));
}

// The folder path of a "folder:" row: "Rock/Live".
QString folderOf(const QString &rowId)
{
    return afterPrefix(rowId, QStringLiteral("folder:"));
}

// A name in double quotes, as the messages show it.
QString quoted(const QString &text)
{
    QString out;
    out.append(QLatin1Char('"'));
    out.append(text);
    out.append(QLatin1Char('"'));
    return out;
}

// The hooks that are used when the caller gives none: the window's own dialogs.
PlaylistEditing::Hooks defaultHooks(MainWindow *window)
{
    PlaylistEditing::Hooks hooks;
    hooks.askName
        = [window](const QString &title, const QString &initial) -> std::optional<QString> {
        bool ok = false;
        const QString name = QInputDialog::getText(
            window, title, QStringLiteral("Name:"), QLineEdit::Normal, initial, &ok);
        if (!ok)
            return std::nullopt;
        return name;
    };
    hooks.chooseFolder = [window](const QStringList &folders) -> std::optional<QString> {
        QStringList items;
        items.append(QString()); // the top
        items.append(folders);
        bool ok = false;
        const QString folder = QInputDialog::getItem(window, QStringLiteral("Move to Folder"),
            QStringLiteral("Folder (blank for the top):"), items, 0, true, &ok);
        if (!ok)
            return std::nullopt;
        return folder;
    };
    hooks.confirm = [window](const QString &question) {
        return QMessageBox::question(window, QStringLiteral("Delete Playlist"), question,
                   QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            == QMessageBox::Yes;
    };
    hooks.showMenu
        = [](const QStringList &commands, const QStringList &labels, const QPoint &globalPos) {
              QMenu menu;
              for (qsizetype i = 0; i < commands.size() && i < labels.size(); ++i)
                  menu.addAction(labels.at(i))->setData(commands.at(i));
              const QAction *picked = menu.exec(globalPos);
              return picked ? picked->data().toString() : QString();
          };
    return hooks;
}

}

struct PlaylistEditing::Private {
    MainWindow *window = nullptr;
    LibraryShell *shell = nullptr;
    PlaylistStore *playlists = nullptr;
    Hooks hooks;
    QString message; // PE-MESSAGE: the last outcome that was reported

    // PE-MESSAGE: shows the text in the status bar and keeps it.
    void report(const QString &text)
    {
        message = text;
        window->statusBar()->showMessage(text);
    }

    // The name a playlist is listed under: its last part, "Best of" for "Rock/Best of".
    QString nameOf(const QString &id) const
    {
        for (const PlaylistInfo &info : playlists->playlists()) {
            if (info.id == id)
                return info.name;
        }
        return QString();
    }

    // Whether the navigator's choice is this playlist.
    bool chosen(const QString &id) const
    {
        return shell->navigator()->currentNode() == QStringLiteral("playlist:") + id;
    }

    // PE-COMMANDS, new: asks for a name, then makes the playlist in the row's folder. No answer
    // ends the command with nothing changed.
    bool create(const QString &rowId)
    {
        if (!hooks.askName)
            return false;
        const std::optional<QString> name
            = hooks.askName(QStringLiteral("New Playlist"), QStringLiteral("New Playlist"));
        if (!name)
            return false;
        QString error;
        const QString id = playlists->create(*name, folderOf(rowId), &error);
        if (id.isEmpty()) {
            report(QStringLiteral("Could not create the playlist: ") + error);
            return false;
        }
        report(QStringLiteral("Created the playlist ") + quoted(id));
        shell->navigator()->selectNode(QStringLiteral("playlist:") + id);
        return true;
    }

    // PE-COMMANDS, rename: asks for the new name, then renames the playlist. If it was the choice,
    // the playlist under its new id is the choice afterwards.
    bool renamePlaylist(const QString &rowId)
    {
        if (!hooks.askName)
            return false;
        const QString id = playlistIdOf(rowId);
        const std::optional<QString> name
            = hooks.askName(QStringLiteral("Rename Playlist"), nameOf(id));
        if (!name)
            return false;
        const bool wasChosen = chosen(id);
        QString error;
        const QString newId = playlists->rename(id, *name, &error);
        if (newId.isEmpty()) {
            report(QStringLiteral("Could not rename the playlist: ") + error);
            return false;
        }
        report(QStringLiteral("Renamed the playlist to ") + quoted(newId));
        if (wasChosen)
            shell->navigator()->selectNode(QStringLiteral("playlist:") + newId);
        return true;
    }

    // PE-COMMANDS, move: asks for one of the folders, or the top (the empty text). A folder that
    // does not exist yet is made first.
    bool movePlaylist(const QString &rowId)
    {
        if (!hooks.chooseFolder)
            return false;
        const QString id = playlistIdOf(rowId);
        const std::optional<QString> folder = hooks.chooseFolder(playlists->folders());
        if (!folder)
            return false;
        QString error;
        if (!folder->isEmpty() && !playlists->folders().contains(*folder)) {
            if (!playlists->createFolder(*folder, &error)) {
                report(QStringLiteral("Could not move the playlist: ") + error);
                return false;
            }
        }
        const bool wasChosen = chosen(id);
        const QString newId = playlists->moveToFolder(id, *folder, &error);
        if (newId.isEmpty()) {
            report(QStringLiteral("Could not move the playlist: ") + error);
            return false;
        }
        report(QStringLiteral("Moved the playlist to ") + quoted(newId));
        if (wasChosen)
            shell->navigator()->selectNode(QStringLiteral("playlist:") + newId);
        return true;
    }

    // PE-COMMANDS, delete: asks first. On yes, removes the playlist's file; its tracks stay in
    // the library.
    bool deletePlaylist(const QString &rowId)
    {
        if (!hooks.confirm)
            return false;
        const QString id = playlistIdOf(rowId);
        const QString question = QStringLiteral("Delete the playlist ") + quoted(id)
            + QStringLiteral("? Its tracks stay in the library.");
        if (!hooks.confirm(question))
            return false;
        if (!playlists->remove(id)) {
            report(QStringLiteral("Could not delete the playlist: the file could not be removed"));
            return false;
        }
        report(QStringLiteral("Deleted the playlist ") + quoted(id));
        return true;
    }
};

PlaylistEditing::PlaylistEditing(MainWindow *window, ActionRegistry *actions, LibraryShell *shell,
    PlaylistStore *playlists, Hooks hooks, QObject *parent)
    : QObject(parent)
    , d(new Private)
{
    d->window = window;
    d->shell = shell;
    d->playlists = playlists;
    d->hooks = std::move(hooks);
    const Hooks fallback = defaultHooks(window);
    if (!d->hooks.askName)
        d->hooks.askName = fallback.askName;
    if (!d->hooks.chooseFolder)
        d->hooks.chooseFolder = fallback.chooseFolder;
    if (!d->hooks.confirm)
        d->hooks.confirm = fallback.confirm;
    if (!d->hooks.showMenu)
        d->hooks.showMenu = fallback.showMenu;

    // PE-WIRING: File > Playlists > New Playlist. Destroying this object frees the command.
    actions->implement(QStringLiteral("file.playlists.new"), this,
        [this] { run(QStringLiteral("new"), QStringLiteral("playlists")); });

    // PE-WIRING: a right click on a navigator row offers the commands that row has.
    connect(shell->navigator(), &NavigatorPanel::menuRequested, this,
        [this](const QString &rowId, const QPoint &globalPos) {
            const QStringList commands = commandsFor(rowId);
            if (commands.isEmpty())
                return;
            QStringList labels;
            for (const QString &command : commands)
                labels.append(label(command));
            const QString picked = d->hooks.showMenu(commands, labels, globalPos);
            if (!picked.isEmpty())
                run(picked, rowId);
        });
}

PlaylistEditing::~PlaylistEditing()
{
    delete d;
}

QStringList PlaylistEditing::commandsFor(const QString &rowId) const
{
    if (rowId == QLatin1String("playlists"))
        return { QStringLiteral("new") };
    if (!folderOf(rowId).isEmpty())
        return { QStringLiteral("new") };
    const QString id = playlistIdOf(rowId);
    if (!id.isEmpty() && d->playlists->exists(id))
        return { QStringLiteral("rename"), QStringLiteral("move"), QStringLiteral("delete") };
    return { };
}

QString PlaylistEditing::label(const QString &command)
{
    if (command == QLatin1String("new"))
        return QStringLiteral("New Playlist") + QChar(0x2026);
    if (command == QLatin1String("rename"))
        return QStringLiteral("Rename") + QChar(0x2026);
    if (command == QLatin1String("move"))
        return QStringLiteral("Move to Folder") + QChar(0x2026);
    if (command == QLatin1String("delete"))
        return QStringLiteral("Delete");
    return QString();
}

bool PlaylistEditing::run(const QString &command, const QString &rowId)
{
    if (!commandsFor(rowId).contains(command))
        return false;
    if (command == QLatin1String("new"))
        return d->create(rowId);
    if (command == QLatin1String("rename"))
        return d->renamePlaylist(rowId);
    if (command == QLatin1String("move"))
        return d->movePlaylist(rowId);
    if (command == QLatin1String("delete"))
        return d->deletePlaylist(rowId);
    return false;
}

QString PlaylistEditing::lastMessage() const
{
    return d->message;
}

}
