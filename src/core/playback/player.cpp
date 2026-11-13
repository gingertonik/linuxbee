#include "playback/player_p.h"

// The player's public surface and its wiring to the queue and the backend.
// Contract: docs/spec/player.md. The state machine is in player_p.h, the
// listening and counting in playeraccounting.cpp.
namespace linuxbee {

Player::Player(PlayQueue *queue, AudioBackend *backend, QObject *parent)
    : QObject(parent)
    , d(new Private(this, queue, backend))
{
    connect(queue, &PlayQueue::currentChanged, this,
        [this](int index) { d->onQueueCurrentChanged(index); });
    connect(backend, &AudioBackend::positionChanged, this,
        [this](qint64 ms) { d->onBackendPosition(ms); });
    connect(backend, &AudioBackend::durationChanged, this,
        [this](qint64 ms) { d->onBackendDuration(ms); });
    connect(backend, &AudioBackend::ended, this, [this]() { d->onEnded(); });
    connect(backend, &AudioBackend::failed, this,
        [this](const QString &message) { d->onFailed(message); });
}

Player::State Player::state() const
{
    return d->state;
}

QString Player::currentPath() const
{
    return d->path;
}

qint64 Player::positionMs() const
{
    return d->position;
}

qint64 Player::durationMs() const
{
    return d->duration;
}

double Player::volume() const
{
    return d->volume;
}

bool Player::isMuted() const
{
    return d->muted;
}

bool Player::stopAfterCurrent() const
{
    return d->stopAfterCurrentOn;
}

void Player::setTrackVolume(std::function<double(const QString &path)> factor)
{
    d->setTrackVolume(std::move(factor));
}

void Player::setCountRule(const PlayCountRule &rule)
{
    d->countRule = rule;
}

void Player::play()
{
    d->play();
}

void Player::pause()
{
    d->pause();
}

void Player::playPause()
{
    d->playPause();
}

void Player::stop()
{
    d->stop();
}

void Player::next()
{
    d->next();
}

void Player::previous()
{
    d->previous();
}

void Player::seek(qint64 ms)
{
    d->seek(ms);
}

void Player::setVolume(double volume)
{
    d->setVolume(volume);
}

void Player::setMuted(bool muted)
{
    d->setMuted(muted);
}

void Player::setStopAfterCurrent(bool on)
{
    d->setStopAfterCurrent(on);
}

}
