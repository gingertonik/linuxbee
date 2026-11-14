#include "shell/autoplaylistnodes.h"

#include <QPointer>
#include <QRandomGenerator>

#include <optional>
#include <utility>

#include "library/library.h"
#include "mainwindow/mainwindow.h"
#include "navigator/navigatorpanel.h"
#include "playlists/autoplaylist.h"
#include "playlists/autoplayliststore.h"
#include "shell/libraryshell.h"

// AN-NODES, AN-SHOW: auto-playlists as rows of the navigator and as nodes the
// shell shows. The rules are asked again each time one is shown.
namespace linuxbee::ui {

struct AutoPlaylistNodes::Private {
    MainWindow *window = nullptr;
    LibraryShell *shell = nullptr;
    Library *library = nullptr;
    AutoPlaylistStore *store = nullptr;
    Hooks hooks;
    quint32 seed = 0; // asked once, in the constructor, and kept

    // AN-SHOW: the tracks of the auto-playlist NAME as its rules answer now, over every track
    // of the library. An auto-playlist that cannot be loaded shows no tracks.
    bool tracksOf(const QString &name, QList<LibraryTrack> *tracks)
    {
        tracks->clear();
        AutoPlaylist playlist;
        if (!store->load(name, &playlist, nullptr))
            return true;
        RuleContext context;
        context.now = hooks.now ? hooks.now() : QDateTime::currentDateTimeUtc();
        const QList<qint64> ids = evaluateAutoPlaylist(playlist, library->tracks(), context, seed);
        for (const qint64 id : ids) {
            if (const std::optional<LibraryTrack> track = library->track(id))
                tracks->append(*track);
        }
        return true;
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

    // AN-SHOW: the node source for the ids that begin with "auto:". The shell keeps it for
    // good, so it holds the object only through a QPointer and answers nothing once the
    // object is gone.
    const QPointer<AutoPlaylistNodes> self(this);
    shell->addNodeSource([self](const QString &nodeId, QList<LibraryTrack> *tracks) {
        const QString prefix = QStringLiteral("auto:");
        if (!self || !nodeId.startsWith(prefix))
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
}

AutoPlaylistNodes::~AutoPlaylistNodes()
{
    delete d;
}

QStringList AutoPlaylistNodes::commandsFor(const QString &rowId) const
{
    Q_UNUSED(rowId);
    return { };
}

QString AutoPlaylistNodes::label(const QString &command)
{
    Q_UNUSED(command);
    return { };
}

bool AutoPlaylistNodes::run(const QString &command, const QString &rowId)
{
    Q_UNUSED(command);
    Q_UNUSED(rowId);
    return false;
}

QString AutoPlaylistNodes::lastMessage() const
{
    return { };
}

}
