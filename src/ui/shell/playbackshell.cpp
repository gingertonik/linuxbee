#include "shell/playbackshell.h"

#include <QAction>
#include <QFileInfo>
#include <QSettings>
#include <QStatusBar>

#include "actions/actionregistry.h"
#include "mainwindow/mainwindow.h"
#include "navigator/navigatorpanel.h"
#include "playerbar/playerbar.h"
#include "shell/libraryshell.h"
#include "sound/replaygain.h"

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
    GainOptions::Mode gainMode = GainOptions::Mode::Off;
    // PS-GAIN speed: the factor last worked out, for the path it was worked out for.
    bool kept = false;
    QString keptPath;
    double keptFactor = 1.0;

    // PS-GAIN: the factor for a path under the mode; the library is asked once per path and mode.
    double factorFor(const QString &path)
    {
        if (kept && keptPath == path)
            return keptFactor;
        double factor = 1.0;
        if (const auto track = library->trackByPath(path)) {
            GainOptions options;
            options.mode = gainMode;
            factor = gainFactor(gainFromTags(track->tags), options);
        }
        kept = true;
        keptPath = path;
        keptFactor = factor;
        return factor;
    }

    // PS-GAIN: the one checked command is the mode; the player is given the function again.
    void showGainMode()
    {
        const struct {
            const char16_t *id;
            GainOptions::Mode mode;
        } modes[] = {
            { u"controls.gain.off", GainOptions::Mode::Off },
            { u"controls.gain.track", GainOptions::Mode::Track },
            { u"controls.gain.album", GainOptions::Mode::Album },
        };
        for (const auto &entry : modes) {
            if (QAction *action = actions->action(QString::fromUtf16(entry.id)))
                action->setChecked(entry.mode == gainMode);
        }
    }

    // PS-GAIN: a new mode drops the kept answer and the player is given the function again.
    void applyGainMode()
    {
        kept = false;
        player->setTrackVolume([this](const QString &path) { return factorFor(path); });
    }
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

    // PS-GAIN: the stored mode, or Off for no value or any other.
    const QString storedGain = settings->value(QStringLiteral("playback/gain")).toString();
    if (storedGain == QStringLiteral("track"))
        d->gainMode = GainOptions::Mode::Track;
    else if (storedGain == QStringLiteral("album"))
        d->gainMode = GainOptions::Mode::Album;
    const struct {
        const char16_t *id;
        GainOptions::Mode mode;
        const char *text;
    } gainCommands[] = {
        { u"controls.gain.off", GainOptions::Mode::Off, "off" },
        { u"controls.gain.track", GainOptions::Mode::Track, "track" },
        { u"controls.gain.album", GainOptions::Mode::Album, "album" },
    };
    for (const auto &command : gainCommands) {
        const GainOptions::Mode mode = command.mode;
        const QString text = QString::fromLatin1(command.text);
        actions->implement(QString::fromUtf16(command.id), this, [this, mode, text] {
            // Choosing the mode that is on changes nothing but its tick, which the click toggled.
            if (mode != d->gainMode) {
                d->gainMode = mode;
                d->settings->setValue(QStringLiteral("playback/gain"), text);
                d->applyGainMode();
            }
            d->showGainMode();
        });
    }
    d->showGainMode();
    d->applyGainMode();
    // PS-GAIN: what the library holds for a track changed; the kept answer goes.
    connect(library, &Library::tracksUpdated, this, [this] { d->kept = false; });

    // PS-PLAY: the library shell asks for a list played from a row.
    connect(libraryShell, &LibraryShell::playRequested, this,
        [this](const QStringList &paths, int index) { d->queue->playNow(paths, index); });

    // PS-TRASHED: files that went to the trash leave the queue, but the one playing. The
    // player is told nothing.
    connect(libraryShell, &LibraryShell::filesTrashed, this,
        [this](const QStringList &paths) { d->queue->removePaths(paths); });

    // PS-UNQUEUE: the Delete key in Playing Tracks takes the tracks out of the queue, but the
    // playing one, in one change. The player is told nothing.
    connect(
        libraryShell, &LibraryShell::queueRemovalRequested, this, [this](const QStringList &paths) {
            // Found before the removal: removePaths leaves the current entry where it is.
            const QList<PlayQueue::Entry> before = d->queue->entries();
            const int at = d->queue->currentIndex();
            const bool currentNamed
                = at >= 0 && at < before.size() && paths.contains(before.at(at).path);
            const int n = d->queue->removePaths(paths);
            QString message;
            if (n > 0 && !currentNamed)
                message = QStringLiteral("Removed %1 from the queue").arg(n);
            else if (n > 0)
                message
                    = QStringLiteral("Removed %1 from the queue; the playing track stays").arg(n);
            else if (currentNamed)
                message = QStringLiteral("The playing track stays in the queue");
            else
                return;
            d->lastMessage = message;
            d->window->statusBar()->showMessage(message);
        });

    // PS-NODE: a playlist the navigator reports activated is chosen first if it is
    // not on show, then played whole, from its first track, in its own order. An
    // auto-playlist is chosen the same way, and the queue plays the tracks the list
    // shows, in the order shown, from the first. Any other node, or a playlist with
    // no track the library knows, or an auto-playlist that shows nothing, plays
    // nothing.
    connect(d->libraryShell->navigator(), &NavigatorPanel::nodeActivated, this,
        [this](const QString &nodeId) {
            const QString automatic = QStringLiteral("auto:");
            const QString prefix = QStringLiteral("playlist:");
            const bool isAuto = nodeId.startsWith(automatic) && nodeId.size() > automatic.size();
            if (!isAuto && (!nodeId.startsWith(prefix) || nodeId.size() <= prefix.size()))
                return;
            NavigatorPanel *navigator = d->libraryShell->navigator();
            if (navigator->currentNode() != nodeId && !navigator->selectNode(nodeId))
                return;
            QStringList paths;
            if (isAuto) {
                // An auto-playlist has no order of its own: what the list shows, in its order.
                paths = d->libraryShell->shownPaths();
            } else {
                for (const LibraryTrack &track : d->libraryShell->nodeTracks())
                    paths.append(track.path);
            }
            if (!paths.isEmpty())
                d->queue->playNow(paths, 0);
        });

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
