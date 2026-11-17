#pragma once
#include <QHash>
#include <QObject>
#include <QString>

#include <functional>

#include "playback/playqueue.h"

// The player: what Play, Pause, Next and the end of a track do, and when a
// play or a skip is counted. Contract: docs/spec/player.md (X-x).
// It makes no sound itself. An AudioBackend does; the player tells it what
// to do and listens to what it reports.
namespace linuxbee {

// One file at a time. Implemented over an audio library elsewhere, and by a
// recording stand-in in the tests.
// A backend never emits a signal from inside one of the calls below: what a
// call leads to (a failure included) is reported from the event loop
// afterwards. The player relies on this (docs/spec/player.md, X-BACKEND).
class AudioBackend : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void load(const QString &path) = 0; // gets ready at position 0; does not start
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0; // lets go of the file
    virtual void seek(qint64 ms) = 0;
    virtual void setVolume(double linear) = 0; // 0 to 1

signals:
    void positionChanged(qint64 ms);
    void durationChanged(qint64 ms);
    void ended(); // the file played to its end
    void failed(const QString &message); // the file cannot be played
};

class Player : public QObject {
    Q_OBJECT
public:
    enum class State { Stopped, Playing, Paused };
    Q_ENUM(State)

    // Neither pointer is owned; both must outlive the player.
    Player(PlayQueue *queue, AudioBackend *backend, QObject *parent = nullptr);

    State state() const;
    QString currentPath() const; // the track last started; empty before the first
    qint64 positionMs() const;
    qint64 durationMs() const; // 0 until the backend says
    double volume() const; // 0 to 1
    bool isMuted() const;
    bool stopAfterCurrent() const;

    // X-VOLUME: a factor per track (volume levelling). By default 1 for all.
    void setTrackVolume(std::function<double(const QString &path)> factor);
    int renamePaths(const QHash<QString, QString> &oldToNew); // X-RENAME
    void setCountRule(const PlayCountRule &rule);

public slots:
    void play();
    void pause();
    void playPause();
    void stop();
    void next();
    void previous();
    void seek(qint64 ms);
    void setVolume(double volume);
    void setMuted(bool muted);
    void setStopAfterCurrent(bool on);

signals:
    void stateChanged(linuxbee::Player::State state);
    void trackChanged(const QString &path); // empty: no track any more
    void positionChanged(qint64 ms);
    void durationChanged(qint64 ms);
    void volumeChanged(double volume);
    void mutedChanged(bool muted);
    void stopAfterCurrentChanged(bool on);
    // X-COUNT
    void playCounted(const QString &path);
    void skipCounted(const QString &path);
    void playbackError(const QString &path, const QString &message);

private:
    struct Private;
    Private *d;
};

}
