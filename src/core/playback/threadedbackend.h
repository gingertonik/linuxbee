#pragma once
#include <functional>

#include "playback/player.h"

// LOCKED. Written by the reviewer. Contract: docs/spec/audio-thread.md (AT-x).
//
// An audio backend on a thread of its own. The player talks to this one from
// its own thread and never waits; the backend that does the work (and its
// sink) is made, used and destroyed on the other thread, so sound goes on
// while the window is busy, and a slow file never holds the window.
namespace linuxbee {

class ThreadedAudioBackend : public AudioBackend {
    Q_OBJECT
public:
    // Run once, on the audio thread. What it returns is owned by this object
    // and destroyed on the audio thread. Make the sink inside it too.
    using Factory = std::function<AudioBackend *()>;

    explicit ThreadedAudioBackend(Factory factory, QObject *parent = nullptr);
    ~ThreadedAudioBackend() override; // AT-END

    void load(const QString &path) override;
    void play() override;
    void pause() override;
    void stop() override;
    void seek(qint64 ms) override;
    void setVolume(double linear) override;

private:
    struct Private;
    Private *d;
};

}
