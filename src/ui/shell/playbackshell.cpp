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

    // PS-COMMANDS: registered now; their work comes with the next task.
    actions->implement(QStringLiteral("track.playNow"), this, [] { });
    actions->implement(QStringLiteral("track.queueNext"), this, [] { });
    actions->implement(QStringLiteral("track.queueLast"), this, [] { });
}

PlaybackShell::~PlaybackShell()
{
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
    Q_UNUSED(path);
    return { };
}

QString PlaybackShell::lastMessage() const
{
    return d->lastMessage;
}

void PlaybackShell::saveState()
{
}

}
