#pragma once
#include "playback/player.h"

#include <utility>

// The state the player keeps, shared by player.cpp (the state machine) and
// playeraccounting.cpp (X-LISTEN and X-COUNT). Not part of the public API.
namespace linuxbee {

// It is a QObject child of the player, so the player owns it and deletes it
// (the header declares no destructor).
struct Player::Private : public QObject {
    // X-NEXT: Previous goes back to the start of a track that is more than
    // this far in (in ms), and to the track before otherwise.
    static constexpr qint64 kRestartPastMs = 3000;

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
    qint64 position = 0; // the last report or seek (the listening starts here)
    qint64 duration = 0; // 0 until the backend says
    double volume = 1.0;
    bool muted = false;
    bool stopAfterCurrentOn = false;
    bool ownQueueMove = false; // true only while the player itself moves the queue
    std::function<double(const QString &path)> trackVolume; // X-VOLUME; empty: 1
    PlayCountRule countRule; // Q-COUNT, set by setCountRule
    qint64 heardMs = 0; // X-LISTEN: time heard of the current play
    bool accounted = false; // X-COUNT: the current play has been counted

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

    // X-SEEK: a duration is kept, and passed on when it changes.
    void setDuration(qint64 ms)
    {
        if (ms == duration)
            return;
        duration = ms;
        emit q->durationChanged(ms);
    }

    void onBackendDuration(qint64 ms)
    {
        if (state != State::Stopped)
            setDuration(ms);
    }

    // X-VOLUME: the backend gets 0 when muted, else the volume times the
    // track's factor (a factor below 0 counts as 0), held between 0 and 1.
    // With no track there is no factor to ask about, so it is 1.
    void applyVolume()
    {
        double factor = 1.0;
        if (trackVolume && !path.isEmpty())
            factor = qMax(0.0, trackVolume(path));
        backend->setVolume(muted ? 0.0 : qBound(0.0, volume * factor, 1.0));
    }

    // X-STATE: load the file, set the volume, play. Position and duration
    // start at 0. trackChanged only for a different queue entry. The
    // previous play is counted first (X-COUNT), if it has not been yet.
    void startEntry(const PlayQueue::Entry &entry)
    {
        accountCurrent(true);
        heardMs = 0;
        accounted = false;
        const bool newTrack = entry.entryId != entryId;
        entryId = entry.entryId;
        path = entry.path;
        setPosition(0);
        setDuration(0);
        backend->load(path);
        applyVolume();
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

    // X-SEEK: the target is held between 0 and the duration (no upper limit
    // while the duration is unknown). Only while Playing or Paused.
    void seek(qint64 ms)
    {
        if (state == State::Stopped)
            return;
        qint64 target = qMax<qint64>(0, ms);
        if (duration > 0)
            target = qMin(target, duration);
        backend->seek(target);
        setPosition(target);
    }

    // X-NEXT: back to the start of the track. Not a new play, so not counted.
    void restartTrack() { seek(0); }

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

    // X-END steps 1 and 3 (step 2 is stop after current). The file played to
    // its end: it is counted, then the queue's automatic Next plays on, and
    // with none the player stops.
    void onEnded()
    {
        if (state == State::Stopped)
            return;
        countStretchToEnd();
        accountCurrent(false);
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
    // The play is counted (X-COUNT) as the user's doing.
    void stop()
    {
        if (state == State::Stopped)
            return;
        accountCurrent(true);
        backend->stop();
        setPosition(0);
        setState(State::Stopped);
    }

    // X-VOLUME: the user's volume, held between 0 and 1. The backend is told
    // at once, with or without a track.
    void setVolume(double next)
    {
        const double clamped = qBound(0.0, next, 1.0);
        if (clamped == volume)
            return;
        volume = clamped;
        emit q->volumeChanged(volume);
        applyVolume();
    }

    void setMuted(bool on)
    {
        if (on == muted)
            return;
        muted = on;
        emit q->mutedChanged(muted);
        applyVolume();
    }

    void setTrackVolume(std::function<double(const QString &path)> factor)
    {
        trackVolume = std::move(factor);
        applyVolume();
    }

    // Defined in playeraccounting.cpp (X-LISTEN, X-COUNT).
    void onBackendPosition(qint64 ms);
    void countStretchToEnd();
    void accountCurrent(bool endedByUser);
};

}
