#include "playback/player.h"

// The player's state machine and the start of a track. Contract:
// docs/spec/player.md (X-STATE, X-QUEUE). The rest of the rules arrive with
// their tasks; until then their functions are still stubs.
namespace linuxbee {

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

    // X-STATE: load the file, set the volume, play. Position and duration
    // start at 0. trackChanged only for a different queue entry.
    void startEntry(const PlayQueue::Entry &entry)
    {
        const bool newTrack = entry.entryId != entryId;
        entryId = entry.entryId;
        path = entry.path;
        position = 0;
        duration = 0;
        backend->load(path);
        backend->setVolume(volume);
        backend->play();
        setState(State::Playing);
        if (newTrack)
            emit q->trackChanged(path);
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
        if (index < 0) {
            ownQueueMove = true;
            index = queue->next(false);
            ownQueueMove = false;
        }
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
        position = 0;
        setState(State::Stopped);
    }
};

Player::Player(PlayQueue *queue, AudioBackend *backend, QObject *parent)
    : QObject(parent)
    , d(new Private(this, queue, backend))
{
    connect(queue, &PlayQueue::currentChanged, this,
        [this](int index) { d->onQueueCurrentChanged(index); });
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
