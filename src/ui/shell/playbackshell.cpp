#include "shell/playbackshell.h"

#include <QFileInfo>
#include <QSettings>
#include <QStatusBar>

#include "actions/actionregistry.h"
#include "mainwindow/mainwindow.h"
#include "playerbar/playerbar.h"
#include "shell/libraryshell.h"

namespace linuxbee::ui {

struct PlaybackShell::Private {
    Library *library = nullptr;
    LibraryShell *libraryShell = nullptr;
    MainWindow *window = nullptr;
    ActionRegistry *actions = nullptr;
    QSettings *settings = nullptr;
    std::function<QDateTime()> now;
    PlayQueue *queue = nullptr;
    Player *player = nullptr;
    PlayerBar *bar = nullptr;
    QString lastMessage;
};

PlaybackShell::PlaybackShell(MainWindow *window, ActionRegistry *actions, Library *library,
    LibraryShell *libraryShell, AudioBackend *backend, QSettings *settings,
    std::function<QDateTime()> now, QObject *parent)
    : QObject(parent)
    , d(new Private)
{
    d->window = window;
    d->actions = actions;
    d->library = library;
    d->libraryShell = libraryShell;
    d->settings = settings;
    d->now = std::move(now);

    // PS-BUILD: the queue and the player are children of the shell; the bar
    // has no parent, the window owns it once it is in the panel.
    d->queue = new PlayQueue(this);
    d->player = new Player(d->queue, backend, this);
    d->bar = new PlayerBar(d->player, d->queue, actions);
    window->setPanelWidget(MainWindow::Panel::PlayerBar, d->bar);
    d->bar->restoreState(*settings); // PB-STORE, at once

    // PS-PLAY: the library shell asks for a list played from a row.
    connect(libraryShell, &LibraryShell::playRequested, this,
        [this](const QStringList &paths, int index) { d->queue->playNow(paths, index); });

    // PS-COMMANDS: on the library shell's selection, in the order shown; an
    // empty selection does nothing.
    actions->implement(QStringLiteral("track.playNow"), this, [this] {
        const QStringList paths = d->libraryShell->selectedPaths();
        if (!paths.isEmpty())
            d->queue->playNow(paths, 0);
    });
    actions->implement(QStringLiteral("track.queueNext"), this, [this] {
        const QStringList paths = d->libraryShell->selectedPaths();
        if (!paths.isEmpty())
            d->queue->queueNext(paths);
    });
    actions->implement(QStringLiteral("track.queueLast"), this, [this] {
        const QStringList paths = d->libraryShell->selectedPaths();
        if (!paths.isEmpty())
            d->queue->queueLast(paths);
    });

    // PS-PLAYING: whenever the queue's entries change, the Playing Tracks
    // node is told their paths in queue order.
    auto showQueue = [this] {
        QStringList paths;
        for (const PlayQueue::Entry &entry : d->queue->entries())
            paths.append(entry.path);
        d->libraryShell->setPlayingPaths(paths);
    };
    connect(d->queue, &PlayQueue::entriesChanged, this, showQueue);
    showQueue();

    // PS-TITLE: the bar is told the tag title when the track changes and the
    // library knows one; otherwise the bar keeps its own fallback. The bar
    // was connected first, so this runs after the bar's own handler.
    connect(d->player, &Player::trackChanged, this, [this](const QString &path) {
        const QString title = titleFor(path);
        if (!title.isEmpty())
            d->bar->setTrackText(title);
    });

    // PS-COUNT: a play or a skip the player counts goes to the library, at
    // the shell's clock. A path the library does not know is ignored.
    connect(d->player, &Player::playCounted, this, [this](const QString &path) {
        if (const auto track = d->library->trackByPath(path))
            d->library->recordPlay(track->id, d->now());
    });
    connect(d->player, &Player::skipCounted, this, [this](const QString &path) {
        if (const auto track = d->library->trackByPath(path))
            d->library->recordSkip(track->id, d->now());
    });

    // PS-ERROR: the status bar says which file failed and why. Nothing is counted.
    connect(d->player, &Player::playbackError, this,
        [this](const QString &path, const QString &message) {
            d->lastMessage = QStringLiteral("Cannot play ") + QFileInfo(path).fileName()
                + QStringLiteral(": ") + message;
            d->window->statusBar()->showMessage(d->lastMessage);
        });
}

PlaybackShell::~PlaybackShell()
{
    // PS-BUILD: the sound stops first. Then the bar leaves the window, which
    // deletes it while the player still exists, and its commands go with it.
    // The shell's own commands go when this object is destroyed.
    d->player->stop();
    d->window->setPanelWidget(MainWindow::Panel::PlayerBar, nullptr);
    delete d;
}

PlayQueue *PlaybackShell::queue() const
{
    return d->queue;
}

Player *PlaybackShell::player() const
{
    return d->player;
}

PlayerBar *PlaybackShell::bar() const
{
    return d->bar;
}

QString PlaybackShell::titleFor(const QString &path) const
{
    // PS-TITLE: "Artist - Title", every artist joined as the tag reader joins
    // them; the title alone without an artist; empty without a title.
    const auto track = d->library->trackByPath(path);
    if (!track)
        return { };
    const QString title = track->tags.first(QStringLiteral("Title"));
    if (title.isEmpty())
        return { };
    const QString artist = track->tags.joined(QStringLiteral("Artist"));
    return artist.isEmpty() ? title : artist + QStringLiteral(" - ") + title;
}

QString PlaybackShell::lastMessage() const
{
    return d->lastMessage;
}

void PlaybackShell::saveState()
{
    // PS-STORE: the bar's state (PB-STORE). The queue is not remembered.
    d->bar->saveState(*d->settings);
}

}
