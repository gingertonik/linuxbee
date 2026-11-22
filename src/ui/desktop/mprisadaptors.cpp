#include "desktop/mprisservice.h"
#include "desktop/mprisservice_p.h"

#include <QFileInfo>
#include <QUrl>

#include <random>

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

// MP-PROPS: the queue's repeat, in the words the desktop uses.
QString PlayerAdaptor::loopStatus() const
{
    switch (m_state.queue->repeat()) {
    case PlayQueue::Repeat::All:
        return QStringLiteral("Playlist");
    case PlayQueue::Repeat::One:
        return QStringLiteral("Track");
    case PlayQueue::Repeat::Off:
        return QStringLiteral("None");
    }
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
    return m_state.queue->shuffle();
}

QVariantMap PlayerAdaptor::metadata() const
{
    return metadataOf(m_state);
}

double PlayerAdaptor::volume() const
{
    return m_state.player->volume();
}

// MP-SEEK: the player's position, in microseconds on the bus; 0 when stopped.
qlonglong PlayerAdaptor::position() const
{
    return qlonglong(m_state.player->positionMs()) * 1000;
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

// MP-CHANGED: the properties a change can move, under the names the bus uses,
// read by the same functions the bus reads them with.
QVariantMap PlayerAdaptor::announcedProperties() const
{
    QVariantMap map;
    map.insert(QStringLiteral("PlaybackStatus"), playbackStatus());
    map.insert(QStringLiteral("LoopStatus"), loopStatus());
    map.insert(QStringLiteral("Rate"), rate());
    map.insert(QStringLiteral("Shuffle"), shuffle());
    map.insert(QStringLiteral("Metadata"), metadata());
    map.insert(QStringLiteral("Volume"), volume());
    map.insert(QStringLiteral("MinimumRate"), minimumRate());
    map.insert(QStringLiteral("MaximumRate"), maximumRate());
    map.insert(QStringLiteral("CanGoNext"), canGoNext());
    map.insert(QStringLiteral("CanGoPrevious"), canGoPrevious());
    map.insert(QStringLiteral("CanPlay"), canPlay());
    map.insert(QStringLiteral("CanPause"), canPause());
    map.insert(QStringLiteral("CanSeek"), canSeek());
    map.insert(QStringLiteral("CanControl"), canControl());
    return map;
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

// MP-METHODS: a file URL of a file that is there plays now, alone in the
// queue; anything else is accepted and changes nothing.
void PlayerAdaptor::OpenUri(const QString &uri)
{
    const QUrl url(uri);
    if (url.scheme() != QLatin1String("file") || !url.isLocalFile())
        return;
    const QString path = url.toLocalFile();
    if (!QFileInfo(path).isFile())
        return;
    m_state.queue->playNow({ path }, 0);
}

// MP-SEEK: Seek moves by the offset from where the track is now. Before the
// start is the start; past the end is the next track; nothing happens when
// there is nothing to seek in.
void PlayerAdaptor::Seek(qlonglong offsetUs)
{
    if (!canSeek())
        return;
    const qint64 target = m_state.player->positionMs() + offsetUs / 1000;
    if (target > m_state.player->durationMs()) {
        m_state.player->next();
        return;
    }
    const qint64 landing = target < 0 ? 0 : target;
    m_state.player->seek(landing);
    // MP-SEEK: a seek that lands on the very start is announced as 0; the
    // player's own report of 0 is a track starting, and says nothing.
    if (landing == 0)
        emit Seeked(0);
}

// MP-SEEK: SetPosition goes to a place in a track the caller names. A track
// it does not name, a place outside the track, and any place at all in a
// track that cannot be sought in are all ignored. The length itself is inside.
void PlayerAdaptor::SetPosition(const QDBusObjectPath &trackId, qlonglong positionUs)
{
    if (!canSeek())
        return;
    if (trackId.path() != currentTrackIdOf(m_state))
        return;
    if (positionUs < 0)
        return;
    // MP-SEEK: compared in microseconds, before the dividing: the end of a
    // 200 s track is 200000000, and 200000001 is past it.
    const qint64 lengthUs = m_state.player->durationMs() * 1000;
    if (positionUs > lengthUs)
        return;
    m_state.player->seek(positionUs / 1000);
    if (positionUs == 0)
        emit Seeked(0);
}

// MP-PROPS: the writers. A property that only reports the player's state has
// no writer, so the bus refuses the write, as it should.
void PlayerAdaptor::setLoopStatus(const QString &text)
{
    // MP-PROPS: the three words of the queue's repeat. Any other text changes
    // nothing, and the write is still accepted: it asked for something
    // linuxBee has no name for.
    if (text == QStringLiteral("Playlist"))
        m_state.queue->setRepeat(PlayQueue::Repeat::All);
    else if (text == QStringLiteral("Track"))
        m_state.queue->setRepeat(PlayQueue::Repeat::One);
    else if (text == QStringLiteral("None"))
        m_state.queue->setRepeat(PlayQueue::Repeat::Off);
}

void PlayerAdaptor::setShuffle(bool on)
{
    // MP-PROPS: switching shuffle starts a fresh round, so it gets a fresh seed.
    std::random_device source;
    m_state.queue->setShuffle(on, quint32(source()));
}

void PlayerAdaptor::setVolume(double volume)
{
    // MP-PROPS: the player holds the volume between 0 and 1, so below nothing
    // is nothing and above full is full.
    m_state.player->setVolume(volume);
}

// MP-PROPS: a write of Rate is accepted and ignored: 1.0 is the only speed.
void PlayerAdaptor::setRate(double rate)
{
    Q_UNUSED(rate);
}

}
