#include "desktop/mprisservice.h"
#include "desktop/mprisservice_p.h"

// The two MPRIS interfaces, seen from the bus. They hold no state of their
// own: they read and write the player and the queue through DesktopState.
// Contract: docs/spec/desktop-mpris.md (MP-x).
namespace linuxbee::ui {

RootAdaptor::RootAdaptor(MprisService *service, DesktopState &state)
    : QDBusAbstractAdaptor(service)
    , m_state(state)
{
}

QString RootAdaptor::identity() const
{
    return QStringLiteral("linuxBee");
}

QString RootAdaptor::desktopEntry() const
{
    return QStringLiteral("io.github.gingertonik.linuxbee");
}

bool RootAdaptor::canQuit() const
{
    return true;
}

bool RootAdaptor::canRaise() const
{
    return true;
}

bool RootAdaptor::hasTrackList() const
{
    return false;
}

QStringList RootAdaptor::supportedUriSchemes() const
{
    return { QStringLiteral("file") };
}

QStringList RootAdaptor::supportedMimeTypes() const
{
    return { QStringLiteral("audio/flac"), QStringLiteral("audio/mpeg"),
        QStringLiteral("audio/mp4"), QStringLiteral("audio/ogg"), QStringLiteral("audio/opus"),
        QStringLiteral("audio/x-wav") };
}

// MP-ROOT: the service knows neither the window nor the application, so both
// go through a hook. A hook that was never given is simply not there.
void RootAdaptor::Raise()
{
    if (m_state.hooks.raise)
        m_state.hooks.raise();
}

void RootAdaptor::Quit()
{
    if (m_state.hooks.quit)
        m_state.hooks.quit();
}

PlayerAdaptor::PlayerAdaptor(MprisService *service, DesktopState &state)
    : QDBusAbstractAdaptor(service)
    , m_state(state)
{
}

// MP-PROPS: the player's own state in the words the desktop expects.
QString PlayerAdaptor::playbackStatus() const
{
    switch (m_state.player->state()) {
    case Player::State::Playing:
        return QStringLiteral("Playing");
    case Player::State::Paused:
        return QStringLiteral("Paused");
    case Player::State::Stopped:
        return QStringLiteral("Stopped");
    }
    return QStringLiteral("Stopped");
}

QString PlayerAdaptor::loopStatus() const
{
    return QStringLiteral("None");
}

// MP-PROPS: linuxBee plays at one speed, so all three rates are 1.0.
double PlayerAdaptor::rate() const
{
    return 1.0;
}

double PlayerAdaptor::minimumRate() const
{
    return 1.0;
}

double PlayerAdaptor::maximumRate() const
{
    return 1.0;
}

bool PlayerAdaptor::shuffle() const
{
    return false;
}

QVariantMap PlayerAdaptor::metadata() const
{
    return metadataOf(m_state);
}

double PlayerAdaptor::volume() const
{
    return 1.0;
}

qlonglong PlayerAdaptor::position() const
{
    return 0;
}

// MP-CAN: what the desktop may ask for, read from the queue and the player.
bool PlayerAdaptor::canGoNext() const
{
    // Somewhere to go for a user's Next, not for a track that ended by itself.
    return m_state.queue->hasNext(false);
}

bool PlayerAdaptor::canGoPrevious() const
{
    return m_state.queue->currentIndex() >= 0;
}

bool PlayerAdaptor::canPlay() const
{
    return m_state.queue->count() > 0;
}

bool PlayerAdaptor::canPause() const
{
    return m_state.queue->count() > 0;
}

bool PlayerAdaptor::canSeek() const
{
    // Nowhere to move to while nothing is playing, and no scale to move on
    // the track's length is unknown.
    return m_state.player->state() != Player::State::Stopped && m_state.player->durationMs() > 0;
}

bool PlayerAdaptor::canControl() const
{
    return true;
}

// MP-METHODS: every one of these is the player's function of the same name.
// The player decides what a step means (player.md); the service only passes it on.
void PlayerAdaptor::Play()
{
    m_state.player->play();
}

void PlayerAdaptor::Pause()
{
    m_state.player->pause();
}

void PlayerAdaptor::PlayPause()
{
    m_state.player->playPause();
}

void PlayerAdaptor::Stop()
{
    m_state.player->stop();
}

void PlayerAdaptor::Next()
{
    m_state.player->next();
}

void PlayerAdaptor::Previous()
{
    m_state.player->previous();
}

// MP-METHODS: opening a file the desktop hands over is not in this version
// (docs/spec/desktop-mpris.md, "Not in this version"), so the call is
// accepted and nothing is done with it.
void PlayerAdaptor::OpenUri(const QString &uri)
{
    Q_UNUSED(uri);
}

// MP-PROPS: the writers. A property that only reports the player's state has
// no writer, so the bus refuses the write, as it should.
void PlayerAdaptor::setLoopStatus(const QString &text)
{
    Q_UNUSED(text);
}

void PlayerAdaptor::setShuffle(bool on)
{
    Q_UNUSED(on);
}

void PlayerAdaptor::setVolume(double volume)
{
    Q_UNUSED(volume);
}

// MP-PROPS: a write of Rate is accepted and ignored: 1.0 is the only speed.
void PlayerAdaptor::setRate(double rate)
{
    Q_UNUSED(rate);
}

}
