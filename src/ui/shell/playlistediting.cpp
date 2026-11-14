#include "shell/playlistediting.h"

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
};

PlaylistEditing::PlaylistEditing(MainWindow *window, ActionRegistry *actions, LibraryShell *shell,
    PlaylistStore *playlists, Hooks hooks, QObject *parent)
    : QObject(parent)
    , d(new Private)
{
    Q_UNUSED(actions); // the File menu entry is wired in a later task of this plan
    d->window = window;
    d->shell = shell;
    d->playlists = playlists;
    d->hooks = std::move(hooks);
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
    // rename, move and delete come in the next tasks of this plan.
    return false;
}

QString PlaylistEditing::lastMessage() const
{
    return d->message;
}

}
