#include "playback/threadedbackend.h"

#include <QDebug>
#include <QDeadlineTimer>
#include <QMetaObject>
#include <QThread>

#include <mutex>
#include <utility>

namespace linuxbee {

namespace {

class AudioSide;

// What the audio thread needs, and what is left behind when the destructor gives up waiting for
// it (AT-END). It is made on the caller's thread and freed there only after the audio thread has
// finished; when the thread has not, it is not freed at all.
struct Engine {
    QThread thread; // caller's thread starts and waits for it; the audio thread only runs in it
    AudioSide *side = nullptr; // set before the thread starts; never changed
    // The one thing both threads touch with no posted event between, because a thread stuck in a
    // call cannot be asked anything. `owner` is read and posted to by the audio thread, and set
    // to null by the destructor, both only while holding `lock`: after the destructor has set
    // it, nothing is posted to the object, and a post that was under way has been finished.
    std::mutex lock;
    ThreadedAudioBackend *owner = nullptr;
};

// The part that lives on the audio thread. Its QObject side is only the address calls are posted
// to (this object's thread is the audio thread). Every member is touched by the audio thread only,
// except `engine`, which is written once in the constructor (before the thread starts) and points
// at something that is never freed while the thread runs.
class AudioSide : public QObject {
public:
    explicit AudioSide(Engine *e)
        : engine(e)
    {
    }

    Engine *engine; // set before the thread starts
    AudioBackend *inner = nullptr; // audio thread only: made, used and deleted there
    quint64 handledLoad = 0; // audio thread only: number of the last load or stop handled
    quint64 handledSeek = 0; // audio thread only: number of the last seek handled
};

}

// Which thread owns each member:
//   engine      the pointer: caller's thread only. What it points at is shared as set out on
//               Engine and AudioSide above: the thread is started, waited for and (when it has
//               ended) freed by the caller's thread; when it has not ended in time it is left
//               behind, and never freed.
//   loadCount   caller's thread only: number of load and stop calls made so far.
//   seekCount   caller's thread only: number of seek calls made so far.
//   endWait     caller's thread only: how long the destructor waits.
// A number crosses over only by value, inside a posted event. The one lock is Engine::lock.
struct ThreadedAudioBackend::Private {
    Engine *engine = nullptr;
    quint64 loadCount = 0;
    quint64 seekCount = 0;
    std::chrono::milliseconds endWait { 3000 };
};

ThreadedAudioBackend::ThreadedAudioBackend(Factory factory, QObject *parent)
    : AudioBackend(parent)
    , d(new Private)
{
    d->engine = new Engine;
    Engine *engine = d->engine;
    engine->owner = this;
    engine->side = new AudioSide(engine);
    engine->side->moveToThread(&engine->thread);
    engine->thread.setObjectName(QStringLiteral("audio"));
    AudioSide *side = engine->side;

    // Posted before the thread runs, so it is the first thing handled over there and every later
    // call comes after it (AT-CALLS).
    QMetaObject::invokeMethod(
        side,
        [side, engine, factory = std::move(factory)] {
            AudioBackend *inner = factory(); // AT-OWN: here, on the audio thread
            side->inner = inner;
            if (!inner)
                return;
            // Every report carries the numbers of the last load/stop and seek this side had
            // handled when it was emitted; the caller's thread compares on arrival (AT-STALE,
            // AT-SEEK). The lambdas run on the audio thread (the inner backend's thread).
            auto post = [side, engine](bool position, auto deliver) {
                const quint64 load = side->handledLoad;
                const quint64 seek = side->handledSeek;
                // AT-END: once the destructor has let go of the object, reports go nowhere.
                const std::lock_guard<std::mutex> guard(engine->lock);
                ThreadedAudioBackend *owner = engine->owner;
                if (!owner)
                    return;
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
    engine->thread.start();
}

ThreadedAudioBackend::~ThreadedAudioBackend()
{
    // AT-END: the inner backend goes over there, then the loop ends, then this waits for the
    // thread, for the end wait and no longer.
    Engine *engine = d->engine;
    AudioSide *side = engine->side;
    QMetaObject::invokeMethod(
        side,
        [side] {
            delete side->inner;
            side->inner = nullptr;
            QThread::currentThread()->quit();
        },
        Qt::QueuedConnection);
    const bool ended = engine->thread.wait(QDeadlineTimer(d->endWait));
    {
        // From here nothing is posted to this object. Events already posted to it are removed
        // with it, so nothing arrives afterwards either way.
        const std::lock_guard<std::mutex> guard(engine->lock);
        engine->owner = nullptr;
    }
    if (ended) {
        delete side; // the thread has finished; nothing uses it any more
        delete engine;
    } else {
        // Left behind: the thread, the side and the engine are not freed (a thread cannot be
        // stopped from outside, and QThread must not be destroyed while it runs). The posted
        // event above deletes the inner backend and ends the thread if the call ever comes back.
        qWarning().noquote() << QStringLiteral(
            "ThreadedAudioBackend: the audio thread did not end within %1 ms and is left behind")
                                    .arg(d->endWait.count());
    }
    delete d;
}

void ThreadedAudioBackend::setEndWait(std::chrono::milliseconds wait)
{
    d->endWait = wait;
}

// Each call is numbered on this thread (only this thread writes the numbers) and the numbers go
// across by value. The side stores them before it makes the call, so a report emitted during the
// call already carries the new number.
void ThreadedAudioBackend::load(const QString &path)
{
    const quint64 n = ++d->loadCount;
    AudioSide *side = d->engine->side;
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
    AudioSide *side = d->engine->side;
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
    AudioSide *side = d->engine->side;
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
    AudioSide *side = d->engine->side;
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
    AudioSide *side = d->engine->side;
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
    AudioSide *side = d->engine->side;
    QMetaObject::invokeMethod(
        side,
        [side, linear] {
            if (side->inner)
                side->inner->setVolume(linear);
        },
        Qt::QueuedConnection);
}

}
