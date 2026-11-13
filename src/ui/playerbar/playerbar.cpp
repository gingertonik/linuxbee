#include "playerbar/playerbar_p.h"

#include "actions/actionregistry.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

namespace linuxbee::ui {

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

    // PB-FRAME: the track's name on the first line, with mute and volume at its
    // right end. The second line is previous, play, stop, next, the two times
    // around the seek bar, which takes the spare width. The transport buttons
    // show their command's text, which is wide; with mute and volume beside
    // them the seek bar fell to 367 px at 900 px, under the 400 px the spec asks.
    auto *titleRow = new QHBoxLayout;
    titleRow->addWidget(d->titleLabel, 1);
    titleRow->addWidget(d->muteButton);
    titleRow->addWidget(d->volumeSlider);

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

    // PB-STATE: follow the player as it changes, and start from what it is now.
    Player *p = d->player;
    connect(p, &Player::stateChanged, this, [this] { showState(); });
    connect(
        p, &Player::trackChanged, this, [this] { showLength(0); }); // a new track: length unknown
    connect(p, &Player::durationChanged, this, [this](qint64 ms) { showLength(ms); });
    showLength(p->durationMs());
}

PlayerBar::~PlayerBar()
{
    delete d;
}

void PlayerBar::setTrackText(const QString &text)
{
    Q_UNUSED(text);
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
    const qint64 top = std::clamp<qint64>(ms, 0, std::numeric_limits<int>::max());
    d->seekSlider->setRange(0, static_cast<int>(top));
    d->durationLabel->setText(formatTime(ms));
    showState();
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

void PlayerBar::saveState(QSettings &settings) const
{
    Q_UNUSED(settings);
}

void PlayerBar::restoreState(QSettings &settings)
{
    Q_UNUSED(settings);
}

}
