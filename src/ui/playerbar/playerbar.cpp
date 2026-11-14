#include "playerbar/playerbar_p.h"

#include "actions/actionregistry.h"

#include <QAbstractSlider>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

namespace linuxbee::ui {

namespace {

// A time in ms as the seek bar's int range: clamped to 0 and to the largest int.
int sliderValue(qint64 ms)
{
    return static_cast<int>(std::clamp<qint64>(ms, 0, std::numeric_limits<int>::max()));
}

// PB-MODES: a click moves the repeat mode on by one: off, all, one, off.
PlayQueue::Repeat nextRepeat(PlayQueue::Repeat repeat)
{
    if (repeat == PlayQueue::Repeat::Off)
        return PlayQueue::Repeat::All;
    if (repeat == PlayQueue::Repeat::All)
        return PlayQueue::Repeat::One;
    return PlayQueue::Repeat::Off;
}

// PB-MODES: the repeat button's property, checked state, text and tool tip for a mode.
void showRepeat(QToolButton *button, PlayQueue::Repeat repeat)
{
    QString mode = QStringLiteral("off");
    if (repeat == PlayQueue::Repeat::All)
        mode = QStringLiteral("all");
    else if (repeat == PlayQueue::Repeat::One)
        mode = QStringLiteral("one");
    button->setProperty("mode", mode);
    button->setChecked(repeat != PlayQueue::Repeat::Off);
    button->setText(
        repeat == PlayQueue::Repeat::One ? QStringLiteral("Repeat 1") : QStringLiteral("Repeat"));
    button->setToolTip(QStringLiteral("Repeat: ") + mode);
}

}

PlayerBar::PlayerBar(Player *player, PlayQueue *queue, ActionRegistry *actions, QWidget *parent)
    : QWidget(parent)
    , d(new Private)
{
    d->player = player;
    d->queue = queue;
    d->actions = actions;

    // PB-FRAME: the named widgets.
    d->titleLabel = new QLabel(this);
    d->titleLabel->setObjectName(QStringLiteral("titleLabel"));

    d->previousButton = new QToolButton(this);
    d->previousButton->setObjectName(QStringLiteral("previousButton"));
    d->playPauseButton = new QToolButton(this);
    d->playPauseButton->setObjectName(QStringLiteral("playPauseButton"));
    d->stopButton = new QToolButton(this);
    d->stopButton->setObjectName(QStringLiteral("stopButton"));
    d->nextButton = new QToolButton(this);
    d->nextButton->setObjectName(QStringLiteral("nextButton"));
    d->muteButton = new QToolButton(this);
    d->muteButton->setObjectName(QStringLiteral("muteButton"));

    d->positionLabel = new QLabel(formatTime(0), this);
    d->positionLabel->setObjectName(QStringLiteral("positionLabel"));
    d->durationLabel = new QLabel(formatTime(0), this);
    d->durationLabel->setObjectName(QStringLiteral("durationLabel"));

    // PB-STATE: with nothing playing the seek bar is at 0 and disabled.
    d->seekSlider = new QSlider(Qt::Horizontal, this);
    d->seekSlider->setObjectName(QStringLiteral("seekSlider"));
    d->seekSlider->setRange(0, 0);
    d->seekSlider->setValue(0);
    d->seekSlider->setEnabled(false);

    // PB-VOLUME: the player's volume times 100, rounded.
    d->volumeSlider = new QSlider(Qt::Horizontal, this);
    d->volumeSlider->setObjectName(QStringLiteral("volumeSlider"));
    d->volumeSlider->setRange(0, 100);
    d->volumeSlider->setValue(qRound(d->player->volume() * 100));

    // PB-MODES: repeat (three modes, so no default action) and shuffle (its command),
    // to the right of the volume slider.
    auto *repeatButton = new QToolButton(this);
    repeatButton->setObjectName(QStringLiteral("repeatButton"));
    repeatButton->setCheckable(true);
    auto *shuffleButton = new QToolButton(this);
    shuffleButton->setObjectName(QStringLiteral("shuffleButton"));

    // PB-FRAME: the track's name on the first line, with mute, volume, repeat and
    // shuffle at its right end. The second line is previous, play, stop, next, the two times
    // around the seek bar, which takes the spare width. The transport buttons
    // show their command's text, which is wide; with mute and volume beside
    // them the seek bar fell to 367 px at 900 px, under the 400 px the spec asks.
    auto *titleRow = new QHBoxLayout;
    titleRow->addWidget(d->titleLabel, 1);
    titleRow->addWidget(d->muteButton);
    titleRow->addWidget(d->volumeSlider);
    titleRow->addWidget(repeatButton);
    titleRow->addWidget(shuffleButton);

    auto *controls = new QHBoxLayout;
    controls->addWidget(d->previousButton);
    controls->addWidget(d->playPauseButton);
    controls->addWidget(d->stopButton);
    controls->addWidget(d->nextButton);
    controls->addWidget(d->positionLabel);
    controls->addWidget(d->seekSlider, 1);
    controls->addWidget(d->durationLabel);

    auto *outer = new QVBoxLayout(this);
    outer->addLayout(titleRow);
    outer->addLayout(controls);

    // PB-FRAME: a button is its command. The commands come first, because a
    // button can only take the action of a command that is enabled.
    implementCommands();
    d->previousButton->setDefaultAction(d->actions->action(QStringLiteral("controls.previous")));
    d->playPauseButton->setDefaultAction(d->actions->action(QStringLiteral("controls.playPause")));
    d->stopButton->setDefaultAction(d->actions->action(QStringLiteral("controls.stop")));
    d->nextButton->setDefaultAction(d->actions->action(QStringLiteral("controls.next")));
    d->muteButton->setDefaultAction(d->actions->action(QStringLiteral("controls.mute")));

    // PB-MODES: shuffle is its command, tick included. The repeat button is not:
    // a click moves the queue's mode on, and the button always shows the queue's
    // mode, whoever set it, and at once when the bar is created.
    shuffleButton->setDefaultAction(d->actions->action(QStringLiteral("controls.shuffle")));
    connect(repeatButton, &QToolButton::clicked, this,
        [q = d->queue] { q->setRepeat(nextRepeat(q->repeat())); });
    connect(d->queue, &PlayQueue::modeChanged, this,
        [q = d->queue, repeatButton] { showRepeat(repeatButton, q->repeat()); });
    showRepeat(repeatButton, d->queue->repeat());

    // PB-STATE: follow the player as it changes, and start from what it is now.
    Player *p = d->player;
    connect(p, &Player::stateChanged, this, [this] { showState(); });
    connect(p, &Player::durationChanged, this, [this](qint64 ms) { showLength(ms); });
    // PB-TITLE: the file's name without folder and extension, lasting until the next track.
    d->titleLabel->setText(QFileInfo(p->currentPath()).completeBaseName());
    connect(p, &Player::trackChanged, this, [this](const QString &path) {
        d->titleLabel->setText(QFileInfo(path).completeBaseName());
        showLength(0); // PB-STATE: a new track starts with its length unknown
    });
    showLength(p->durationMs());

    // PB-SEEK: the bar follows the player. It sends a seek only when the user
    // lets go of the handle, or makes a step (a click beside it, a key, the wheel).
    d->seekSlider->setTracking(false);
    connect(p, &Player::positionChanged, this, [this](qint64 ms) { showPosition(ms); });
    connect(d->seekSlider, &QAbstractSlider::sliderReleased, this,
        [this] { seekTo(d->seekSlider->sliderPosition()); });
    connect(d->seekSlider, &QAbstractSlider::actionTriggered, this, [this](int action) {
        if (action
            != QAbstractSlider::SliderMove) // SliderMove is the drag itself: the release seeks
            seekTo(d->seekSlider->sliderPosition());
    });
    showPosition(p->positionMs());
}

PlayerBar::~PlayerBar()
{
    delete d;
}

// PB-TITLE: replaces the name until the next change of track.
void PlayerBar::setTrackText(const QString &text)
{
    d->titleLabel->setText(text);
}

// PB-STATE: the play button's "playing" property, and the seek bar's enabled
// state. A stopped player also puts the seek bar and the time played back to 0.
void PlayerBar::showState()
{
    const Player::State state = d->player->state();
    d->playPauseButton->setProperty("playing", state == Player::State::Playing);
    if (state == Player::State::Stopped) {
        d->seekSlider->setValue(0);
        d->positionLabel->setText(formatTime(0));
    }
    d->seekSlider->setEnabled(state != Player::State::Stopped && d->durationMs > 0);
}

// PB-STATE: the length is the seek bar's range. 0 means not known yet.
void PlayerBar::showLength(qint64 ms)
{
    d->durationMs = ms;
    d->seekSlider->setRange(0, sliderValue(ms));
    d->durationLabel->setText(formatTime(ms));
    showState();
}

// PB-SEEK: the handle and the time played follow the player's position. Following never seeks.
void PlayerBar::showPosition(qint64 ms)
{
    if (d->seekSlider->isSliderDown())
        return; // while the handle is held, the player's reports do not move it
    d->seekSlider->setValue(sliderValue(ms));
    d->positionLabel->setText(formatTime(ms));
}

// PB-SEEK: one Player::seek to where the user put the handle. The bar then shows
// where the player says it is.
void PlayerBar::seekTo(qint64 ms)
{
    d->player->seek(ms);
    const qint64 now = d->player->positionMs();
    d->seekSlider->setValue(sliderValue(now));
    d->positionLabel->setText(formatTime(now));
}

// PB-TIME: whole seconds, rounded down. Under an hour m:ss; from an hour h:mm:ss; below zero 0:00.
QString PlayerBar::formatTime(qint64 ms)
{
    const qint64 total = ms < 0 ? 0 : ms / 1000;
    const qint64 hours = total / 3600;
    const qint64 minutes = (total % 3600) / 60;
    const QString seconds = QString::number(total % 60).rightJustified(2, QLatin1Char('0'));
    if (hours > 0) {
        const QString mm = QString::number(minutes).rightJustified(2, QLatin1Char('0'));
        return QString::number(hours) + QLatin1Char(':') + mm + QLatin1Char(':') + seconds;
    }
    return QString::number(minutes) + QLatin1Char(':') + seconds;
}

}
