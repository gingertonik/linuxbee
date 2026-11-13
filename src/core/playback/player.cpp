#include "playback/player.h"

// Stub. Plan 14b replaces every body in this file.
namespace linuxbee {

struct Player::Private { };

Player::Player(PlayQueue *queue, AudioBackend *backend, QObject *parent)
    : QObject(parent)
    , d(nullptr)
{
    Q_UNUSED(queue);
    Q_UNUSED(backend);
}

Player::State Player::state() const
{
    return State::Stopped;
}

QString Player::currentPath() const
{
    return { };
}

qint64 Player::positionMs() const
{
    return 0;
}

qint64 Player::durationMs() const
{
    return 0;
}

double Player::volume() const
{
    return 0.0;
}

bool Player::isMuted() const
{
    return false;
}

bool Player::stopAfterCurrent() const
{
    return false;
}

void Player::setTrackVolume(std::function<double(const QString &path)> factor)
{
    Q_UNUSED(factor);
}

void Player::setCountRule(const PlayCountRule &rule)
{
    Q_UNUSED(rule);
    Q_UNUSED(d);
}

void Player::play()
{
}
void Player::pause()
{
}
void Player::playPause()
{
}
void Player::stop()
{
}
void Player::next()
{
}
void Player::previous()
{
}

void Player::seek(qint64 ms)
{
    Q_UNUSED(ms);
}

void Player::setVolume(double volume)
{
    Q_UNUSED(volume);
}

void Player::setMuted(bool muted)
{
    Q_UNUSED(muted);
}

void Player::setStopAfterCurrent(bool on)
{
    Q_UNUSED(on);
}

}
