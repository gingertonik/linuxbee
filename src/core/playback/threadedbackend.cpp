#include "playback/threadedbackend.h"

#include <QMetaObject>
#include <QThread>

#include <utility>

namespace linuxbee {

namespace {

// The part that lives on the audio thread. Its QObject side is only the address calls are posted
// to (this object's thread is the audio thread). Every member is touched by the audio thread only,
// except `owner`, which is written once in the constructor (before the thread starts) and is only
// ever used as the target of posted events, never dereferenced for a call.
class AudioSide : public QObject {
public:
    explicit AudioSide(ThreadedAudioBackend *o)
        : owner(o)
    {
    }

    ThreadedAudioBackend *owner; // posted to only; set before the thread starts
    AudioBackend *inner = nullptr; // audio thread only: made, used and deleted there
    quint64 handledLoad = 0; // audio thread only: number of the last load or stop handled
    quint64 handledSeek = 0; // audio thread only: number of the last seek handled
};

}

// Which thread owns each member:
//   thread      caller's thread starts, waits for and destroys it; the audio thread only runs in
//   it. side        made on the caller's thread before the thread starts, moved to the audio
//   thread;
//               after that the audio thread uses it, the caller's thread only posts events to it
//               and deletes it once the thread has finished.
//   loadCount   caller's thread only: number of load and stop calls made so far.
//   seekCount   caller's thread only: number of seek calls made so far.
// No lock: a number crosses over only by value, inside a posted event.
struct ThreadedAudioBackend::Private {
    QThread thread;
    AudioSide *side = nullptr;
    quint64 loadCount = 0;
    quint64 seekCount = 0;
};

ThreadedAudioBackend::ThreadedAudioBackend(Factory factory, QObject *parent)
    : AudioBackend(parent)
    , d(new Private)
{
    d->side = new AudioSide(this);
    d->side->moveToThread(&d->thread);
    d->thread.setObjectName(QStringLiteral("audio"));
    AudioSide *side = d->side;
    ThreadedAudioBackend *owner = this;

    // Posted before the thread runs, so it is the first thing handled over there and every later
    // call comes after it (AT-CALLS).
    QMetaObject::invokeMethod(
        side,
        [side, owner, factory = std::move(factory)] {
            AudioBackend *inner = factory(); // AT-OWN: here, on the audio thread
            side->inner = inner;
            if (!inner)
                return;
            // Every report carries the numbers of the last load/stop and seek this side had
            // handled when it was emitted; the caller's thread compares on arrival (AT-STALE,
            // AT-SEEK). The lambdas run on the audio thread (the inner backend's thread).
            auto post = [side, owner](bool position, auto deliver) {
                const quint64 load = side->handledLoad;
                const quint64 seek = side->handledSeek;
                QMetaObject::invokeMethod(
                    owner,
                    [owner, position, load, seek, deliver] {
                        // Runs on the caller's thread: loadCount and seekCount are its own.
                        if (load < owner->d->loadCount)
                            return;
                        if (position && seek < owner->d->seekCount)
                            return;
                        deliver(owner);
                    },
                    Qt::QueuedConnection);
            };
            QObject::connect(inner, &AudioBackend::positionChanged, side, [post](qint64 ms) {
                post(true, [ms](ThreadedAudioBackend *o) { emit o->positionChanged(ms); });
            });
            QObject::connect(inner, &AudioBackend::durationChanged, side, [post](qint64 ms) {
                post(false, [ms](ThreadedAudioBackend *o) { emit o->durationChanged(ms); });
            });
            QObject::connect(inner, &AudioBackend::ended, side,
                [post] { post(false, [](ThreadedAudioBackend *o) { emit o->ended(); }); });
            QObject::connect(inner, &AudioBackend::failed, side, [post](const QString &message) {
                post(false, [message](ThreadedAudioBackend *o) { emit o->failed(message); });
            });
        },
        Qt::QueuedConnection);
    d->thread.start();
}

ThreadedAudioBackend::~ThreadedAudioBackend()
{
    // AT-END: the inner backend goes over there, then the loop ends, then this waits for the
    // thread. Nothing is delivered after this: the events posted to this object are removed with
    // it, and the thread that posted them has finished.
    AudioSide *side = d->side;
    QMetaObject::invokeMethod(
        side,
        [side] {
            delete side->inner;
            side->inner = nullptr;
            QThread::currentThread()->quit();
        },
        Qt::QueuedConnection);
    d->thread.wait();
    delete side; // the thread has finished; nothing uses it any more
    delete d;
}

// Each call is numbered on this thread (only this thread writes the numbers) and the numbers go
// across by value. The side stores them before it makes the call, so a report emitted during the
// call already carries the new number.
void ThreadedAudioBackend::load(const QString &path)
{
    const quint64 n = ++d->loadCount;
    AudioSide *side = d->side;
    QMetaObject::invokeMethod(
        side,
        [side, n, path] {
            side->handledLoad = n;
            if (side->inner)
                side->inner->load(path);
        },
        Qt::QueuedConnection);
}

void ThreadedAudioBackend::play()
{
    AudioSide *side = d->side;
    QMetaObject::invokeMethod(
        side,
        [side] {
            if (side->inner)
                side->inner->play();
        },
        Qt::QueuedConnection);
}

void ThreadedAudioBackend::pause()
{
    AudioSide *side = d->side;
    QMetaObject::invokeMethod(
        side,
        [side] {
            if (side->inner)
                side->inner->pause();
        },
        Qt::QueuedConnection);
}

void ThreadedAudioBackend::stop()
{
    const quint64 n = ++d->loadCount;
    AudioSide *side = d->side;
    QMetaObject::invokeMethod(
        side,
        [side, n] {
            side->handledLoad = n;
            if (side->inner)
                side->inner->stop();
        },
        Qt::QueuedConnection);
}

void ThreadedAudioBackend::seek(qint64 ms)
{
    const quint64 n = ++d->seekCount;
    AudioSide *side = d->side;
    QMetaObject::invokeMethod(
        side,
        [side, n, ms] {
            side->handledSeek = n;
            if (side->inner)
                side->inner->seek(ms);
        },
        Qt::QueuedConnection);
}

void ThreadedAudioBackend::setVolume(double linear)
{
    AudioSide *side = d->side;
    QMetaObject::invokeMethod(
        side,
        [side, linear] {
            if (side->inner)
                side->inner->setVolume(linear);
        },
        Qt::QueuedConnection);
}

}
