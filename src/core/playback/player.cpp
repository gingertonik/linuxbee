#include "playback/player.h"

// The player's state machine, the start of a track, moving through the
// queue, and the end of a track. Contract: docs/spec/player.md (X-STATE,
// X-QUEUE, X-NEXT, X-END). The rest of the rules arrive with their tasks;
// until then their functions are still stubs.
namespace linuxbee {

namespace {
// X-NEXT: Previous goes back to the start of a track that is more than this
// far in (in ms), and to the track before otherwise.
constexpr qint64 kRestartPastMs = 3000;
}

// The state the player keeps. It is a QObject child of the player, so the
// player owns it and deletes it (the header declares no destructor).
struct Player::Private : public QObject {
    Private(Player *owner, PlayQueue *queueIn, AudioBackend *backendIn)
        : QObject(owner)
        , q(owner)
        , queue(queueIn)
        , backend(backendIn)
    {
    }

    Player *q;
    PlayQueue *queue;
    AudioBackend *backend;
    State state = State::Stopped;
    QString path; // the track last started
    qint64 entryId = 0; // the queue entry last started; 0 for none yet
    qint64 position = 0;
    qint64 duration = 0;
    double volume = 1.0;
    bool muted = false;
    bool stopAfterCurrentOn = false;
    bool ownQueueMove = false; // true only while the player itself moves the queue

    void setState(State next)
    {
        if (next == state)
            return;
        state = next;
        emit q->stateChanged(state);
    }

    // X-SEEK: a position is kept, and passed on when it changes.
    void setPosition(qint64 ms)
    {
        if (ms == position)
            return;
        position = ms;
        emit q->positionChanged(ms);
    }

    // Reports that arrive while nothing is being played are ignored.
    void onBackendPosition(qint64 ms)
    {
        if (state != State::Stopped)
            setPosition(ms);
    }

    // X-STATE: load the file, set the volume, play. Position and duration
    // start at 0. trackChanged only for a different queue entry.
    void startEntry(const PlayQueue::Entry &entry)
    {
        const bool newTrack = entry.entryId != entryId;
        entryId = entry.entryId;
        path = entry.path;
        setPosition(0);
        duration = 0;
        backend->load(path);
        backend->setVolume(volume);
        backend->play();
        setState(State::Playing);
        if (newTrack)
            emit q->trackChanged(path);
    }

    // The player's own queue moves. The queue announces them through
    // currentChanged, and that announcement must not start the track again.
    int nextFromQueue(bool automatic)
    {
        ownQueueMove = true;
        const int index = queue->next(automatic);
        ownQueueMove = false;
        return index;
    }

    int previousFromQueue()
    {
        ownQueueMove = true;
        const int index = queue->previous();
        ownQueueMove = false;
        return index;
    }

    // X-QUEUE: a different current entry, made by anyone but the player, is
    // started whatever the state. Nothing current (a removed entry) changes
    // nothing here.
    void onQueueCurrentChanged(int index)
    {
        if (ownQueueMove || index < 0)
            return;
        const QList<PlayQueue::Entry> entries = queue->entries();
        const PlayQueue::Entry &entry = entries.at(index);
        if (entry.entryId != entryId)
            startEntry(entry);
    }

    // X-NEXT: back to the start of the track (a seek to 0).
    void restartTrack()
    {
        backend->seek(0);
        setPosition(0);
    }

    // X-NEXT: the user's Next, whatever the state. Nowhere to go: nothing.
    void next()
    {
        const int index = nextFromQueue(false);
        if (index < 0)
            return;
        const QList<PlayQueue::Entry> entries = queue->entries();
        startEntry(entries.at(index));
    }

    // X-NEXT: restart a track more than three seconds in; otherwise the
    // track before, and with none, restart the track.
    void previous()
    {
        if (state != State::Stopped && position > kRestartPastMs) {
            restartTrack();
            return;
        }
        const int index = previousFromQueue();
        if (index >= 0) {
            const QList<PlayQueue::Entry> entries = queue->entries();
            startEntry(entries.at(index));
            return;
        }
        if (state != State::Stopped)
            restartTrack();
    }

    // X-END steps 1 and 3 (counting is X-COUNT; step 2 is stop after
    // current). The file played to its end: the queue's automatic Next plays
    // on, and with none the player stops.
    void onEnded()
    {
        if (state == State::Stopped)
            return;
        const int index = nextFromQueue(true);
        if (index < 0) {
            stop();
            return;
        }
        const QList<PlayQueue::Entry> entries = queue->entries();
        startEntry(entries.at(index));
    }

    void play()
    {
        if (state == State::Playing)
            return;
        if (state == State::Paused) {
            backend->play();
            setState(State::Playing);
            return;
        }
        // Stopped: the current entry, or with none, the one Next would go to.
        int index = queue->currentIndex();
        if (index < 0)
            index = nextFromQueue(false);
        if (index < 0)
            return;
        const QList<PlayQueue::Entry> entries = queue->entries();
        startEntry(entries.at(index));
    }

    void pause()
    {
        if (state != State::Playing)
            return;
        backend->pause();
        setState(State::Paused);
    }

    void playPause()
    {
        if (state == State::Playing)
            pause();
        else
            play();
    }

    // X-STATE: the queue keeps its current entry and the track stays on show.
    void stop()
    {
        if (state == State::Stopped)
            return;
        backend->stop();
        setPosition(0);
        setState(State::Stopped);
    }
};

Player::Player(PlayQueue *queue, AudioBackend *backend, QObject *parent)
    : QObject(parent)
    , d(new Private(this, queue, backend))
{
    connect(queue, &PlayQueue::currentChanged, this,
        [this](int index) { d->onQueueCurrentChanged(index); });
    connect(backend, &AudioBackend::positionChanged, this,
        [this](qint64 ms) { d->onBackendPosition(ms); });
    connect(backend, &AudioBackend::ended, this, [this]() { d->onEnded(); });
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
    Q_UNUSED(factor);
}

void Player::setCountRule(const PlayCountRule &rule)
{
    Q_UNUSED(rule);
    Q_UNUSED(d);
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
