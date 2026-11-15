#include "shell/scanrunner.h"

#include <QMetaObject>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <thread>

// SR-THREADS, SR-SCAN, SR-STOP. The looking runs on a thread of its own and
// touches no library; everything that touches the library, and every signal,
// happens on the runner's thread, reached by queued calls.
namespace linuxbee::ui {

namespace {

// What the runner's thread and the looking thread share for one scan.
struct Shared {
    std::atomic<bool> stop { false };
    std::atomic<long long> lastPostMs { -1000 }; // when `progress` was last handed over
};

long long nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}

struct ScanRunner::Private {
    struct Job {
        QStringList folders;
        ScanOptions options;
    };

    ScanRunner *q = nullptr;
    Library *library = nullptr;
    ScanRunnerHooks hooks;
    int partSize = 200;

    std::deque<Job> waiting; // asked for, not begun
    bool running = false; // a scan is under way, or waiting
    bool gone = false; // the destructor has begun
    Job current;
    quint64 serial = 0; // names the scan under way; a call for an older one is dropped
    std::shared_ptr<Shared> shared;
    std::thread looking;
    ScanReport report; // of the scan under way, the parts added up
    int lastDone = 0;
    int lastTotal = 0;

    void begin();
    void startLooking();
    void onProgress(quint64 forScan, int done, int total);
    void onPart(quint64 forScan, const ScanFindings &part);
    void onFound(quint64 forScan, const ScanFindings &findings);
    void end(const ScanReport &result);
    void joinLooking();
};

void ScanRunner::Private::joinLooking()
{
    if (looking.joinable())
        looking.join();
}

void ScanRunner::Private::begin()
{
    // Takes the next scan asked for; `running` is already true.
    current = waiting.front();
    waiting.pop_front();
    ++serial;
    shared = std::make_shared<Shared>();
    report = ScanReport();
    lastDone = 0;
    lastTotal = 0;
    emit q->started(current.folders);
    if (gone)
        return;
    // SR-SCAN: a scan the library would refuse looks at no file.
    if (!current.options.now.isValid()
        || !Library::categories().contains(current.options.category)) {
        const quint64 mine = serial;
        QMetaObject::invokeMethod(
            q,
            [this, mine] {
                if (!gone && mine == serial)
                    end(ScanReport());
            },
            Qt::QueuedConnection);
        return;
    }
    startLooking();
}

void ScanRunner::Private::startLooking()
{
    // SR-THREADS: what the library knows is taken here, on its own thread.
    const ScanKnown known = library->scanKnown();
    const quint64 mine = serial;
    const std::shared_ptr<Shared> state = shared;
    const auto hook = hooks.looking;
    const QStringList folders = current.folders;
    ScanRunner *const runner = q;
    Private *const self = this;

    ScanFindOptions find;
    find.partSize = std::max(1, partSize);
    find.progress = [=](int done, int total) {
        // The hook first, then whether it was told to stop (SR-THREADS).
        if (hook)
            hook(done, total);
        if (state->stop.load())
            return false;
        const long long at = nowMs();
        if (done == 0 || at - state->lastPostMs.load() >= 50) {
            state->lastPostMs = at;
            QMetaObject::invokeMethod(
                runner, [=] { self->onProgress(mine, done, total); }, Qt::QueuedConnection);
        }
        return true;
    };
    find.part = [=](const ScanFindings &part) {
        QMetaObject::invokeMethod(runner, [=] { self->onPart(mine, part); }, Qt::QueuedConnection);
    };

    looking = std::thread([=] {
        // No function of the library is called from here.
        const ScanFindings findings = findForScan(folders, known, find);
        QMetaObject::invokeMethod(
            runner, [=] { self->onFound(mine, findings); }, Qt::QueuedConnection);
    });
}

void ScanRunner::Private::onProgress(quint64 forScan, int done, int total)
{
    if (gone || forScan != serial || !running)
        return;
    lastDone = done;
    lastTotal = total;
    emit q->progress(done, total);
}

void ScanRunner::Private::onPart(quint64 forScan, const ScanFindings &part)
{
    // What was handed over is stored, even after a stop was asked for.
    if (gone || forScan != serial || !running)
        return;
    ScanOptions options = current.options;
    options.progress = nullptr;
    report = addedUp(report, library->applyScan(part, options));
}

void ScanRunner::Private::onFound(quint64 forScan, const ScanFindings &findings)
{
    if (gone || forScan != serial || !running)
        return;
    ScanOptions options = current.options;
    options.progress = nullptr;
    ScanReport result = addedUp(report, library->applyScan(findings, options));
    // SR-STOP: a stop asked for while the scan ran ends it as stopped.
    if (shared->stop.load())
        result.cancelled = true;
    end(result);
}

void ScanRunner::Private::end(const ScanReport &result)
{
    joinLooking(); // it has returned, or is about to
    report = result;
    // SR-SCAN: a scan that was not stopped says its last progress.
    if (!result.cancelled && (lastDone != lastTotal || lastTotal == 0))
        emit q->progress(lastTotal, lastTotal);
    lastDone = lastTotal;
    const QStringList folders = current.folders;
    emit q->finished(result, folders);
    if (gone)
        return;
    if (!waiting.empty()) {
        begin();
        return;
    }
    running = false;
    emit q->idle();
}

ScanRunner::ScanRunner(Library *library, ScanRunnerHooks hooks, QObject *parent)
    : QObject(parent)
    , d(new Private)
{
    d->q = this;
    d->library = library;
    d->hooks = std::move(hooks);
}

ScanRunner::~ScanRunner()
{
    // SR-STOP: tell the looking to stop and wait for that thread before
    // anything is freed; nothing is stored or signalled from here on.
    d->gone = true;
    if (d->shared)
        d->shared->stop = true;
    d->joinLooking();
    delete d;
}

bool ScanRunner::isRunning() const
{
    return d->running;
}

int ScanRunner::waiting() const
{
    return static_cast<int>(d->waiting.size());
}

int ScanRunner::partSize() const
{
    return d->partSize;
}

void ScanRunner::setPartSize(int files)
{
    d->partSize = std::max(1, files);
}

void ScanRunner::scan(const QStringList &folders, const ScanOptions &options)
{
    d->waiting.push_back({ folders, options });
    if (d->running)
        return;
    d->running = true;
    d->begin();
}

void ScanRunner::cancel()
{
    if (!d->running)
        return;
    d->waiting.clear();
    if (d->shared)
        d->shared->stop = true;
}

}
