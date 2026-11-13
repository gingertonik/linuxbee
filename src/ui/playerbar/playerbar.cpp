#include "playerbar/playerbar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

namespace linuxbee::ui {

struct PlayerBar::Private {
    Player *player = nullptr;
    PlayQueue *queue = nullptr;
    ActionRegistry *actions = nullptr;
    QLabel *titleLabel = nullptr;
    QToolButton *previousButton = nullptr;
    QToolButton *playPauseButton = nullptr;
    QToolButton *stopButton = nullptr;
    QToolButton *nextButton = nullptr;
    QToolButton *muteButton = nullptr;
    QLabel *positionLabel = nullptr;
    QSlider *seekSlider = nullptr;
    QLabel *durationLabel = nullptr;
    QSlider *volumeSlider = nullptr;
};

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

    // PB-FRAME: the title on its own line; then previous, play, stop, next, the
    // times around the seek bar (which takes the spare width), mute and volume.
    auto *controls = new QHBoxLayout;
    controls->addWidget(d->previousButton);
    controls->addWidget(d->playPauseButton);
    controls->addWidget(d->stopButton);
    controls->addWidget(d->nextButton);
    controls->addWidget(d->positionLabel);
    controls->addWidget(d->seekSlider, 1);
    controls->addWidget(d->durationLabel);
    controls->addWidget(d->muteButton);
    controls->addWidget(d->volumeSlider);

    auto *outer = new QVBoxLayout(this);
    outer->addWidget(d->titleLabel);
    outer->addLayout(controls);
}

PlayerBar::~PlayerBar()
{
    delete d;
}

void PlayerBar::setTrackText(const QString &text)
{
    Q_UNUSED(text);
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
