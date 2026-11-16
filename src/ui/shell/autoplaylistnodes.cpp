#include "shell/autoplaylistnodes.h"

#include <QAction>
#include <QHash>
#include <QMenu>
#include <QMessageBox>
#include <QPoint>
#include <QPointer>
#include <QRandomGenerator>
#include <QStatusBar>

#include <utility>

#include "library/library.h"
#include "mainwindow/mainwindow.h"
#include "navigator/navigatorpanel.h"
#include "playlists/autoplaylist.h"
#include "playlists/autoplayliststore.h"
#include "shell/libraryshell.h"

// AN-NODES, AN-SHOW, AN-MENU: auto-playlists as rows of the navigator and as
// nodes the shell shows. The rules are asked again each time one is shown.
namespace linuxbee::ui {

namespace {

QString quoted(const QString &name)
{
    return QLatin1Char('"') + name + QLatin1Char('"');
}

// The name of an auto-playlist row ("auto:NAME"); empty for every other row.
QString nameOf(const QString &rowId)
{
    const QString prefix = QStringLiteral("auto:");
    return rowId.startsWith(prefix) ? rowId.mid(prefix.size()) : QString();
}

// Without hooks: a pop-up menu with one entry per command. Returns the command chosen, or empty.
QString popUpMenu(const QStringList &commands, const QStringList &labels, const QPoint &globalPos,
    QWidget *parent)
{
    QMenu menu(parent);
    for (int i = 0; i < commands.size(); ++i)
        menu.addAction(labels.at(i))->setData(commands.at(i));
    const QAction *chosen = menu.exec(globalPos);
    return chosen ? chosen->data().toString() : QString();
}

}

struct AutoPlaylistNodes::Private {
    MainWindow *window = nullptr;
    LibraryShell *shell = nullptr;
    Library *library = nullptr;
    AutoPlaylistStore *store = nullptr;
    Hooks hooks;
    quint32 seed = 0; // asked once, in the constructor, and kept
    bool gone = false; // set while the object goes away; the node source answers nothing then
    QString lastMessage; // AN-MENU: what the last command said

    // AN-SHOW: the tracks of the auto-playlist NAME as its rules answer now, over every track
    // of the library. An auto-playlist that cannot be loaded shows no tracks.
    bool tracksOf(const QString &name, QList<LibraryTrack> *tracks)
    {
        tracks->clear();
        AutoPlaylist playlist;
        QString why;
        if (!store->load(name, &playlist, &why)) {
            // AN-SHOW: the status bar says why it cannot be shown, with the store's reason.
            window->statusBar()->showMessage(QStringLiteral("Cannot show the auto-playlist ")
                + quoted(name) + QStringLiteral(": ") + why);
            return true;
        }
        RuleContext context;
        context.now = hooks.now ? hooks.now() : QDateTime::currentDateTimeUtc();
        // AN-SPEED: the library is read once; the shown tracks come out of that answer.
        const QList<LibraryTrack> all = library->tracks();
        const QList<qint64> ids = evaluateAutoPlaylist(playlist, all, context, seed);
        QHash<qint64, qsizetype> position;
        position.reserve(all.size());
        for (qsizetype i = 0; i < all.size(); ++i)
            position.insert(all.at(i).id, i);
        for (const qint64 id : ids) {
            const auto found = position.constFind(id);
            if (found != position.constEnd())
                tracks->append(all.at(found.value()));
        }
        return true;
    }

    // AN-MENU: Delete, once the user has said yes. The message is said after the store has
    // changed, because the navigator's change of choice shows the library again.
    bool deleteAuto(const QString &name)
    {
        const QString question = QStringLiteral("Delete the auto-playlist ") + quoted(name)
            + QStringLiteral("? Its tracks stay in the library.");
        if (!hooks.confirm(question))
            return false;
        if (!store->remove(name)) {
            say(QStringLiteral(
                "Could not delete the auto-playlist: the file could not be removed"));
            return false;
        }
        say(QStringLiteral("Deleted the auto-playlist ") + quoted(name));
        return true;
    }

    void say(const QString &text)
    {
        lastMessage = text;
        window->statusBar()->showMessage(text);
    }
};

AutoPlaylistNodes::AutoPlaylistNodes(MainWindow *window, LibraryShell *shell, Library *library,
    AutoPlaylistStore *store, Hooks hooks, QObject *parent)
    : QObject(parent)
    , d(new Private)
{
    d->window = window;
    d->shell = shell;
    d->library = library;
    d->store = store;
    d->hooks = std::move(hooks);
    // AN-SHOW: the seed is asked once, here, and used for the object's whole life.
    d->seed = d->hooks.seed ? d->hooks.seed() : QRandomGenerator::global()->generate();
    // AN-MENU: without hooks, a yes-or-no question and a pop-up menu.
    if (!d->hooks.confirm) {
        d->hooks.confirm = [window](const QString &question) {
            return QMessageBox::question(window, QStringLiteral("Auto-playlists"), question)
                == QMessageBox::Yes;
        };
    }
    if (!d->hooks.showMenu) {
        d->hooks.showMenu = [window](const QStringList &commands, const QStringList &labels,
                                const QPoint &globalPos) {
            return popUpMenu(commands, labels, globalPos, window);
        };
    }

    // AN-SHOW: the node source for the ids that begin with "auto:". The shell keeps it for
    // good, so it holds the object only through a QPointer and answers nothing once the
    // object is gone.
    const QPointer<AutoPlaylistNodes> self(this);
    shell->addNodeSource([self](const QString &nodeId, QList<LibraryTrack> *tracks) {
        const QString prefix = QStringLiteral("auto:");
        if (!self || self->d->gone || !nodeId.startsWith(prefix))
            return false;
        return self->d->tracksOf(nodeId.mid(prefix.size()), tracks);
    });

    // AN-NODES: the navigator lists the store's names, now and at every change of the list.
    const auto showList = [this] { d->shell->navigator()->setAutoPlaylists(d->store->names()); };
    showList();
    connect(store, &AutoPlaylistStore::listChanged, this, showList);

    // AN-SHOW: the one shown is read again when the store says it was saved again. Others are
    // not disturbed.
    connect(store, &AutoPlaylistStore::changed, this, [this](const QString &name) {
        if (d->shell->currentNode() == QStringLiteral("auto:") + name)
            d->shell->reloadNode();
    });

    // AN-MENU: a context menu asked for on a row offers that row's commands; the chosen one runs.
    connect(shell->navigator(), &NavigatorPanel::menuRequested, this,
        [this](const QString &rowId, const QPoint &globalPos) {
            const QStringList commands = commandsFor(rowId);
            if (commands.isEmpty())
                return;
            QStringList labels;
            for (const QString &command : commands)
                labels.append(label(command));
            const QString chosen = d->hooks.showMenu(commands, labels, globalPos);
            if (!chosen.isEmpty())
                run(chosen, rowId);
        });
}

AutoPlaylistNodes::~AutoPlaylistNodes()
{
    // AN-NODES: the rows leave the navigator, and the node source answers nothing from now on.
    d->gone = true;
    d->shell->navigator()->setAutoPlaylists({ });
    delete d;
}

QStringList AutoPlaylistNodes::commandsFor(const QString &rowId) const
{
    // AN-MENU: an auto-playlist the store has offers Delete; no other row offers anything.
    const QString name = nameOf(rowId);
    if (!name.isEmpty() && d->store->exists(name))
        return { QStringLiteral("delete") };
    return { };
}

QString AutoPlaylistNodes::label(const QString &command)
{
    return command == QStringLiteral("delete") ? QStringLiteral("Delete") : QString();
}

bool AutoPlaylistNodes::run(const QString &command, const QString &rowId)
{
    // AN-MENU: only Delete, and only for an auto-playlist the store has.
    const QString name = nameOf(rowId);
    if (command != QStringLiteral("delete") || name.isEmpty() || !d->store->exists(name))
        return false;
    return d->deleteAuto(name);
}

QString AutoPlaylistNodes::lastMessage() const
{
    return d->lastMessage;
}

}
