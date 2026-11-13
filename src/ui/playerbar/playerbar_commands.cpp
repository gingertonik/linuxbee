#include "playerbar/playerbar_p.h"

#include "actions/actionregistry.h"

#include <QAction>
#include <QRandomGenerator>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>

#include <algorithm>

namespace linuxbee::ui {

namespace {

// PB-VOLUME: one step of whole points, stopping at 0 and at 100.
void stepVolume(Player *player, int points)
{
    const int now = qRound(player->volume() * 100);
    player->setVolume(std::clamp(now + points, 0, 100) / 100.0);
}

// PB-STORE: the repeat mode as it is written in the settings file.
QString repeatText(PlayQueue::Repeat repeat)
{
    if (repeat == PlayQueue::Repeat::All)
        return QStringLiteral("all");
    if (repeat == PlayQueue::Repeat::One)
        return QStringLiteral("one");
    return QStringLiteral("off");
}

// PB-STORE: a stored repeat mode; anything else is no repeat.
PlayQueue::Repeat repeatFromText(const QString &text)
{
    if (text == QLatin1String("all"))
        return PlayQueue::Repeat::All;
    if (text == QLatin1String("one"))
        return PlayQueue::Repeat::One;
    return PlayQueue::Repeat::Off;
}

}

// PB-COMMANDS: the controls.* commands. The bar gives each one its behaviour
// while it exists; the registry takes that back when the bar is destroyed.
void PlayerBar::implementCommands()
{
    ActionRegistry *actions = d->actions;
    Player *player = d->player;
    PlayQueue *queue = d->queue;
    QSlider *volume = d->volumeSlider;
    QAction *mute = actions->action(QStringLiteral("controls.mute"));
    QAction *stopAfter = actions->action(QStringLiteral("controls.stopAfterCurrent"));
    QAction *repeat = actions->action(QStringLiteral("controls.repeat"));
    QAction *shuffle = actions->action(QStringLiteral("controls.shuffle"));

    actions->implement(
        QStringLiteral("controls.playPause"), this, [player] { player->playPause(); });
    actions->implement(QStringLiteral("controls.stop"), this, [player] { player->stop(); });
    actions->implement(QStringLiteral("controls.next"), this, [player] { player->next(); });
    actions->implement(QStringLiteral("controls.previous"), this, [player] { player->previous(); });

    // The tick-boxes. The registry runs each function after Qt has flipped the
    // tick, so the function reads isChecked() for the new state.
    actions->implement(QStringLiteral("controls.mute"), this,
        [player, mute] { player->setMuted(mute->isChecked()); });
    actions->implement(QStringLiteral("controls.stopAfterCurrent"), this,
        [player, stopAfter] { player->setStopAfterCurrent(stopAfter->isChecked()); });
    actions->implement(QStringLiteral("controls.repeat"), this, [queue, repeat] {
        queue->setRepeat(repeat->isChecked() ? PlayQueue::Repeat::All : PlayQueue::Repeat::Off);
    });
    actions->implement(QStringLiteral("controls.shuffle"), this, [queue, shuffle] {
        queue->setShuffle(shuffle->isChecked(), QRandomGenerator::global()->generate());
    });

    // PB-VOLUME: the volume steps are 5 points, stopping at 0 and 100.
    actions->implement(
        QStringLiteral("controls.volumeUp"), this, [player] { stepVolume(player, 5); });
    actions->implement(
        QStringLiteral("controls.volumeDown"), this, [player] { stepVolume(player, -5); });

    // PB-COMMANDS: a tick always shows the truth, whoever changed it. Setting a
    // tick with setChecked does not run its command.
    mute->setChecked(player->isMuted());
    stopAfter->setChecked(player->stopAfterCurrent());
    repeat->setChecked(queue->repeat() != PlayQueue::Repeat::Off);
    shuffle->setChecked(queue->shuffle());
    connect(player, &Player::mutedChanged, this, [mute](bool muted) { mute->setChecked(muted); });
    connect(player, &Player::stopAfterCurrentChanged, this,
        [stopAfter](bool on) { stopAfter->setChecked(on); });
    connect(queue, &PlayQueue::modeChanged, this, [queue, repeat, shuffle] {
        // Repeat is on for any mode, repeat-one included.
        repeat->setChecked(queue->repeat() != PlayQueue::Repeat::Off);
        shuffle->setChecked(queue->shuffle());
    });

    // PB-VOLUME: the slider shows the player's volume times 100, rounded. Moving
    // it sets the volume. A volume set elsewhere moves it without an echo.
    connect(player, &Player::volumeChanged, this, [volume](double level) {
        const QSignalBlocker blocker(volume);
        volume->setValue(qRound(level * 100));
    });
    connect(volume, &QSlider::valueChanged, this,
        [player](int value) { player->setVolume(value / 100.0); });
}

// PB-STORE: the group `player` holds volume (0 to 100), muted, repeat (off, all
// or one) and shuffle.
void PlayerBar::saveState(QSettings &settings) const
{
    settings.beginGroup(QStringLiteral("player"));
    settings.setValue(QStringLiteral("volume"), qRound(d->player->volume() * 100));
    settings.setValue(QStringLiteral("muted"), d->player->isMuted());
    settings.setValue(QStringLiteral("repeat"), repeatText(d->queue->repeat()));
    settings.setValue(QStringLiteral("shuffle"), d->queue->shuffle());
    settings.endGroup();
}

// PB-STORE: applies the stored values to the player and the queue. A missing or
// senseless value gives full volume, not muted, no repeat, no shuffle.
void PlayerBar::restoreState(QSettings &settings)
{
    settings.beginGroup(QStringLiteral("player"));
    bool known = false;
    int level = settings.value(QStringLiteral("volume")).toInt(&known);
    if (!known || level < 0 || level > 100)
        level = 100;
    const bool muted = settings.value(QStringLiteral("muted")).toBool();
    const PlayQueue::Repeat repeat
        = repeatFromText(settings.value(QStringLiteral("repeat")).toString());
    const bool shuffle = settings.value(QStringLiteral("shuffle")).toBool();
    settings.endGroup();

    d->player->setVolume(level / 100.0);
    d->player->setMuted(muted);
    d->queue->setRepeat(repeat);
    d->queue->setShuffle(shuffle, QRandomGenerator::global()->generate());
}

}
