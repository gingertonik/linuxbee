#include "shell/scanrunner.h"

#include <QMetaObject>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <thread>

// SR-THREADS, SR-SCAN, SR-STOP, SR-TURN. The looking runs on a thread of its own and
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

    // SR-TURN: what the looking handed over, waiting to be stored one a turn.
    struct Item {
        quint64 forScan = 0;
        bool last = false; // the looking's last findings, stored behind its parts
        ScanFindings findings;
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
    bool refused = false; // SR-REFUSED: the library refused a part; nothing more goes to it
    bool cancelAsked = false; // SR-STOP: cancel() was called for the scan under way
    int lastDone = 0;
    int lastTotal = 0;
    std::deque<Item> handedOver; // SR-TURN: in the order handed over
    QTimer storeTimer; // SR-TURN: single shot, 0 ms; stores one waiting item per firing
    bool storing = false; // inside storeOne: the next timer is armed after it, not before

    void begin();
    void wait(quint64 forScan, bool last, const ScanFindings &findings);
    void storeOne();
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
    refused = false;
    cancelAsked = false;
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
    const LibraryRead<ScanKnown> read = library->readScanKnown();
    const quint64 mine = serial;
    if (read.failed()) {
        // SR-REFUSED, L-READFAIL 4: what the library knows cannot be read (it has said `failed`,
        // once until a whole reading succeeds). The scan looks at no file and ends from the event
        // loop, as a refused scan does, with the words; it is not cancelled.
        const QString words = read.words();
        QMetaObject::invokeMethod(
            q,
            [this, mine, words] {
                if (gone || mine != serial)
                    return;
                ScanReport refusedReport;
                refusedReport.refused = words;
                refusedReport.startUnread = true; // SR-REFUSED, L-READFAIL 4
                end(refusedReport);
            },
            Qt::QueuedConnection);
        return;
    }
    const ScanKnown known = read.value();
    const std::shared_ptr<Shared> state = shared;
    const auto hook = hooks.looking;
    const QStringList folders = current.folders;
    ScanRunner *const runner = q;
    Private *const self = this;

    ScanFindOptions find;
    find.emptyMeansGone = current.options.emptyMeansGone; // L-UNPLUGGED
    find.sweep = !current.options.nobodyWaits; // L-SETASIDE
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
        QMetaObject::invokeMethod(
            runner, [=] { self->wait(mine, false, part); }, Qt::QueuedConnection);
    };

    looking = std::thread([=] {
        // No function of the library is called from here.
        const ScanFindings findings = findForScan(folders, known, find);
        QMetaObject::invokeMethod(
            runner, [=] { self->wait(mine, true, findings); }, Qt::QueuedConnection);
    });
}

void ScanRunner::Private::wait(quint64 forScan, bool last, const ScanFindings &findings)
{
    // SR-TURN: nothing is stored from the queued call itself; it waits for its turn.
    if (gone)
        return;
    handedOver.push_back({ forScan, last, findings });
    if (!storing && !storeTimer.isActive())
        storeTimer.start();
}

void ScanRunner::Private::storeOne()
{
    if (gone || handedOver.empty())
        return;
    Item item = std::move(handedOver.front());
    handedOver.pop_front();
    storing = true;
    if (item.last)
        onFound(item.forScan, item.findings);
    else
        onPart(item.forScan, item.findings);
    storing = false;
    // SR-TURN: the next one is armed after this store and its signals have gone out.
    if (!gone && !handedOver.empty() && !storeTimer.isActive())
        storeTimer.start();
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
    // SR-REFUSED: after a refusal no further part of the scan is given to the library.
    if (refused)
        return;
    ScanOptions options = current.options;
    options.progress = nullptr;
    const ScanReport stored = library->applyScan(part, options);
    report = addedUp(report, stored);
    if (!stored.refused.isEmpty()) {
        // SR-REFUSED: the library said why once; the looking is told to stop, as cancel() does.
        refused = true;
        report.refused = stored.refused;
        if (shared)
            shared->stop = true;
    }
}

void ScanRunner::Private::onFound(quint64 forScan, const ScanFindings &findings)
{
    if (gone || forScan != serial || !running)
        return;
    ScanReport result = report;
    // SR-REFUSED: a refused scan gives the library nothing more, not even the last findings.
    if (!refused) {
        ScanOptions options = current.options;
        options.progress = nullptr;
        const ScanReport stored = library->applyScan(findings, options);
        result = addedUp(report, stored);
        result.refused = stored.refused;
    }
    // SR-STOP: a stop asked for while the scan ran ends it as stopped. A refusal sets the same
    // stop flag, so the flag cannot tell the two apart: cancel() is remembered by itself.
    if (cancelAsked)
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
    d->storeTimer.setSingleShot(true);
    d->storeTimer.setInterval(0);
    connect(&d->storeTimer, &QTimer::timeout, this, [this] { d->storeOne(); });
}

ScanRunner::~ScanRunner()
{
    // SR-STOP: tell the looking to stop and wait for that thread before
    // anything is freed; nothing is stored or signalled from here on.
    d->gone = true;
    d->storeTimer.stop();
    d->handedOver.clear();
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
    d->cancelAsked = true;
    if (d->shared)
        d->shared->stop = true;
}

}
