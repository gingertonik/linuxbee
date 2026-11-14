// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/convert-batch.md (B-x). Every test
// but the last replaces the converter with one that only waits, so no
// encoder runs; the last converts the plan-08 fixtures for real.
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

#include <atomic>

#include "convert/convertbatch.h"

using namespace linuxbee;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

QStringList destinations(const QList<ConvertJob> &jobs)
{
    QStringList out;
    for (const ConvertJob &j : jobs)
        out.append(j.destination);
    return out;
}

QList<ConvertJob> jobs(int n)
{
    QList<ConvertJob> out;
    for (int i = 0; i < n; ++i) {
        ConvertJob j;
        j.source = QStringLiteral("/in/%1.flac").arg(i);
        j.destination = QStringLiteral("/out/%1.mp3").arg(i);
        out.append(j);
    }
    return out;
}

// Waits a little instead of converting, and counts how many run at once.
struct Waiter {
    std::atomic<int> now { 0 };
    std::atomic<int> most { 0 };
    std::atomic<int> calls { 0 };
    std::atomic<bool> onMainThread { false };
    QThread *main = QThread::currentThread();
    int ms = 30;
    int failIndex = -1;

    ConvertBatch::Converter converter()
    {
        return [this](const ConvertJob &job, const ConvertProgress &progress) {
            ++calls;
            if (QThread::currentThread() == main)
                onMainThread = true;
            const int running = ++now;
            int seen = most.load();
            while (running > seen && !most.compare_exchange_weak(seen, running)) { }
            ConvertResult r;
            r.status = ConvertResult::Status::Converted;
            r.outputPath = job.destination;
            for (int step = 1; step <= 3; ++step) {
                QThread::msleep(ms / 3);
                if (progress && !progress(step / 3.0)) {
                    r.status = ConvertResult::Status::Cancelled;
                    r.outputPath.clear();
                    break;
                }
            }
            if (job.source == QStringLiteral("/in/%1.flac").arg(failIndex)) {
                r.status = ConvertResult::Status::Failed;
                r.message = QStringLiteral("no good");
            }
            --now;
            return r;
        };
    }
};

}

class ConvertBatchTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qRegisterMetaType<linuxbee::ConvertResult>(); }
    void plan();
    void planNames();
    void planCarriesOptions();
    void parallelSetting();
    void runsEveryJob();
    void neverMoreThanAllowed();
    void oneAtATime();
    void signalsOnOwnThread();
    void failureDoesNotStopTheRest();
    void cancel();
    void startRules();
    void destroyedWhileRunning();
    void realConversion();
};

void ConvertBatchTest::plan()
{
    // B-DEST
    BatchOptions o;
    o.settings.format = AudioFormat::Mp3;
    // Next to the source.
    QList<ConvertJob> j = planBatch({ s(u"/music/a/one.flac"), s(u"/music/b/two.wav") }, o);
    QCOMPARE(j.size(), 2);
    QCOMPARE(j.at(0).source, s(u"/music/a/one.flac"));
    QCOMPARE(destinations(j), QStringList({ s(u"/music/a/one.mp3"), s(u"/music/b/two.mp3") }));
    // Into one folder.
    o.outputFolder = s(u" /out/mp3/ ");
    j = planBatch({ s(u"/music/a/one.flac"), s(u"/music/b/two.wav") }, o);
    QCOMPARE(destinations(j), QStringList({ s(u"/out/mp3/one.mp3"), s(u"/out/mp3/two.mp3") }));
    // Paths are made clean.
    j = planBatch({ s(u"/music/a/../a/./one.flac") }, o);
    QCOMPARE(j.at(0).source, s(u"/music/a/one.flac"));
    QVERIFY(planBatch({ }, o).isEmpty());
    // The extension follows the format.
    o.settings.format = AudioFormat::Aac;
    QCOMPARE(destinations(planBatch({ s(u"/m/x.flac") }, o)), QStringList({ s(u"/out/mp3/x.m4a") }));
    o.settings.format = AudioFormat::OggVorbis;
    QCOMPARE(destinations(planBatch({ s(u"/m/x.flac") }, o)), QStringList({ s(u"/out/mp3/x.ogg") }));
}

void ConvertBatchTest::planNames()
{
    // B-DEST: no two jobs write the same file, and no job writes over a source.
    BatchOptions o;
    o.settings.format = AudioFormat::Mp3;
    o.outputFolder = s(u"/out");
    QCOMPARE(destinations(planBatch({ s(u"/a/song.flac"), s(u"/b/song.flac"), s(u"/c/song.wav"), s(u"/c/other.flac") }, o)),
        QStringList({ s(u"/out/song.mp3"), s(u"/out/song_1.mp3"), s(u"/out/song_2.mp3"), s(u"/out/other.mp3") }));
    // The same format in the same folder would land on the source.
    o.outputFolder.clear();
    o.settings.format = AudioFormat::Flac;
    QCOMPARE(destinations(planBatch({ s(u"/a/song.flac") }, o)), QStringList({ s(u"/a/song_1.flac") }));
    // A source later in the list is also off limits.
    o.settings.format = AudioFormat::Mp3;
    QCOMPARE(destinations(planBatch({ s(u"/a/song.flac"), s(u"/a/song.mp3") }, o)),
        QStringList({ s(u"/a/song_1.mp3"), s(u"/a/song_2.mp3") }));
    // Only the last extension is replaced; a name without one keeps all of it; case matters.
    QCOMPARE(destinations(planBatch({ s(u"/a/live.2019.flac"), s(u"/a/README"), s(u"/a/Song.flac"), s(u"/a/song.flac") }, o)),
        QStringList({ s(u"/a/live.2019.mp3"), s(u"/a/README.mp3"), s(u"/a/Song.mp3"), s(u"/a/song.mp3") }));
    // The same source twice is two jobs.
    QCOMPARE(destinations(planBatch({ s(u"/a/x.flac"), s(u"/a/x.flac") }, o)), QStringList({ s(u"/a/x.mp3"), s(u"/a/x_1.mp3") }));
}

void ConvertBatchTest::planCarriesOptions()
{
    BatchOptions o;
    o.settings = encoderPreset(s(u"mp3-320")).value().settings;
    o.ifExists = ConvertJob::IfExists::Overwrite;
    o.copyTags = true;
    o.copyPictures = false;
    o.tagOptions.id3Version = WriteOptions::Id3Version::V24;
    o.ffmpegPath = s(u"/opt/ffmpeg");
    const ConvertJob j = planBatch({ s(u"/a/x.flac") }, o).value(0);
    QVERIFY(j.settings.format == AudioFormat::Mp3);
    QCOMPARE(j.settings.bitrateKbps, 320);
    QVERIFY(j.ifExists == ConvertJob::IfExists::Overwrite);
    QVERIFY(j.copyTags && !j.copyPictures);
    QVERIFY(j.tagOptions.id3Version == WriteOptions::Id3Version::V24);
    QCOMPARE(j.ffmpegPath, s(u"/opt/ffmpeg"));
    // The defaults are the converter's.
    const BatchOptions plain;
    QVERIFY(plain.ifExists == ConvertJob::IfExists::Skip);
    QVERIFY(plain.copyTags && plain.copyPictures);
}

void ConvertBatchTest::parallelSetting()
{
    // B-PARALLEL
    const int usual = ConvertBatch::defaultParallel();
    QCOMPARE(usual, qBound(1, QThread::idealThreadCount(), 4));
    ConvertBatch b;
    QCOMPARE(b.maxParallel(), usual);
    b.setMaxParallel(2);
    QCOMPARE(b.maxParallel(), 2);
    b.setMaxParallel(0);
    QCOMPARE(b.maxParallel(), 1);
    b.setMaxParallel(99);
    QCOMPARE(b.maxParallel(), 16);
    QVERIFY(!b.isRunning());
    QCOMPARE(b.jobCount(), 0);
    QVERIFY(b.results().isEmpty());
}

void ConvertBatchTest::runsEveryJob()
{
    // B-RUN
    Waiter w;
    ConvertBatch b;
    b.setConverter(w.converter());
    b.setMaxParallel(3);
    QSignalSpy started(&b, &ConvertBatch::jobStarted);
    QSignalSpy progress(&b, &ConvertBatch::jobProgress);
    QSignalSpy done(&b, &ConvertBatch::jobFinished);
    QSignalSpy finished(&b, &ConvertBatch::finished);
    QVERIFY(b.start(jobs(7)));
    QVERIFY(b.isRunning());
    QCOMPARE(b.jobCount(), 7);
    QCOMPARE(finished.size(), 0); // start does not wait
    QVERIFY(finished.wait(5000));
    QVERIFY(!b.isRunning());
    QCOMPARE(w.calls.load(), 7);
    QVERIFY(!w.onMainThread.load()); // the work is done elsewhere
    QCOMPARE(finished.size(), 1);
    // Each job: started once, finished once, and its progress never goes backwards.
    QCOMPARE(started.size(), 7);
    QCOMPARE(done.size(), 7);
    QList<int> startedIds, doneIds;
    for (const auto &e : started)
        startedIds.append(e.at(0).toInt());
    for (const auto &e : done)
        doneIds.append(e.at(0).toInt());
    std::sort(startedIds.begin(), startedIds.end());
    std::sort(doneIds.begin(), doneIds.end());
    QCOMPARE(startedIds, QList<int>({ 0, 1, 2, 3, 4, 5, 6 }));
    QCOMPARE(doneIds, startedIds);
    QCOMPARE(progress.size(), 21);
    QHash<int, double> last;
    for (const auto &e : progress) {
        const int index = e.at(0).toInt();
        const double fraction = e.at(1).toDouble();
        QVERIFY(fraction >= last.value(index, 0.0) && fraction <= 1.0);
        last.insert(index, fraction);
    }
    // The results, by index, whatever order they finished in.
    const QList<ConvertResult> results = b.results();
    QCOMPARE(results.size(), 7);
    for (int i = 0; i < 7; ++i) {
        QVERIFY(results.at(i).status == ConvertResult::Status::Converted);
        QCOMPARE(results.at(i).outputPath, QStringLiteral("/out/%1.mp3").arg(i));
    }
    for (const auto &e : done) {
        const auto r = e.at(1).value<linuxbee::ConvertResult>();
        QCOMPARE(r.outputPath, QStringLiteral("/out/%1.mp3").arg(e.at(0).toInt()));
    }
}

void ConvertBatchTest::neverMoreThanAllowed()
{
    // B-PARALLEL
    Waiter w;
    w.ms = 45;
    ConvertBatch b;
    b.setConverter(w.converter());
    b.setMaxParallel(3);
    QSignalSpy finished(&b, &ConvertBatch::finished);
    QVERIFY(b.start(jobs(12)));
    QVERIFY(finished.wait(10000));
    QCOMPARE(w.calls.load(), 12);
    QVERIFY2(w.most.load() <= 3, qPrintable(QString::number(w.most.load())));
    QVERIFY2(w.most.load() >= 2, "the jobs did not overlap at all"); // and it does use the room
}

void ConvertBatchTest::oneAtATime()
{
    Waiter w;
    w.ms = 9;
    ConvertBatch b;
    b.setConverter(w.converter());
    b.setMaxParallel(1);
    QSignalSpy started(&b, &ConvertBatch::jobStarted);
    QSignalSpy finished(&b, &ConvertBatch::finished);
    QVERIFY(b.start(jobs(6)));
    QVERIFY(finished.wait(5000));
    QCOMPARE(w.most.load(), 1);
    // One at a time means in order.
    QList<int> order;
    for (const auto &e : started)
        order.append(e.at(0).toInt());
    QCOMPARE(order, QList<int>({ 0, 1, 2, 3, 4, 5 }));
}

void ConvertBatchTest::signalsOnOwnThread()
{
    // B-RUN: a listener never has to think about threads.
    Waiter w;
    ConvertBatch b;
    b.setConverter(w.converter());
    b.setMaxParallel(4);
    int wrong = 0, heard = 0;
    auto check = [&] {
        ++heard;
        if (QThread::currentThread() != b.thread())
            ++wrong;
    };
    connect(&b, &ConvertBatch::jobStarted, &b, check, Qt::DirectConnection);
    connect(&b, &ConvertBatch::jobProgress, &b, check, Qt::DirectConnection);
    connect(&b, &ConvertBatch::jobFinished, &b, check, Qt::DirectConnection);
    connect(&b, &ConvertBatch::finished, &b, check, Qt::DirectConnection);
    QSignalSpy finished(&b, &ConvertBatch::finished);
    QVERIFY(b.start(jobs(8)));
    QVERIFY(finished.wait(5000));
    QCOMPARE(heard, 8 + 24 + 8 + 1);
    QCOMPARE(wrong, 0);
    // finished comes after every jobFinished, and results() is complete by then.
    QCOMPARE(b.results().size(), 8);
}

void ConvertBatchTest::failureDoesNotStopTheRest()
{
    Waiter w;
    w.failIndex = 2;
    ConvertBatch b;
    b.setConverter(w.converter());
    b.setMaxParallel(2);
    QSignalSpy finished(&b, &ConvertBatch::finished);
    QVERIFY(b.start(jobs(5)));
    QVERIFY(finished.wait(5000));
    const QList<ConvertResult> r = b.results();
    QCOMPARE(w.calls.load(), 5);
    for (int i = 0; i < 5; ++i)
        QVERIFY(r.at(i).status == (i == 2 ? ConvertResult::Status::Failed : ConvertResult::Status::Converted));
    QCOMPARE(r.at(2).message, s(u"no good"));
}

void ConvertBatchTest::cancel()
{
    // B-CANCEL
    Waiter w;
    w.ms = 60;
    ConvertBatch b;
    b.setConverter(w.converter());
    b.setMaxParallel(2);
    QSignalSpy started(&b, &ConvertBatch::jobStarted);
    QSignalSpy done(&b, &ConvertBatch::jobFinished);
    QSignalSpy finished(&b, &ConvertBatch::finished);
    QVERIFY(b.start(jobs(10)));
    QTRY_VERIFY(started.size() >= 2);
    b.cancel();
    QVERIFY(b.isRunning()); // until the jobs under way have let go
    QVERIFY(finished.wait(5000));
    QVERIFY(!b.isRunning());
    // Every job still gets its one jobFinished; the ones that never ran say Cancelled.
    QCOMPARE(done.size(), 10);
    QVERIFY2(w.calls.load() <= 4, qPrintable(QString::number(w.calls.load())));
    QVERIFY(started.size() == w.calls.load());
    int cancelled = 0;
    for (const ConvertResult &r : b.results()) {
        QVERIFY(r.status == ConvertResult::Status::Cancelled || r.status == ConvertResult::Status::Converted);
        cancelled += r.status == ConvertResult::Status::Cancelled;
    }
    QVERIFY2(cancelled >= 6, qPrintable(QString::number(cancelled)));
    // The batch can be used again afterwards.
    w.ms = 6;
    QVERIFY(b.start(jobs(3)));
    QVERIFY(finished.wait(5000));
    QCOMPARE(b.results().size(), 3);
    for (const ConvertResult &r : b.results())
        QVERIFY(r.status == ConvertResult::Status::Converted);
    b.cancel(); // when nothing runs: nothing happens
    QVERIFY(!b.isRunning());
}

void ConvertBatchTest::startRules()
{
    Waiter w;
    ConvertBatch b;
    b.setConverter(w.converter());
    QSignalSpy finished(&b, &ConvertBatch::finished);
    QVERIFY(!b.start({ })); // nothing to do
    QVERIFY(!b.isRunning());
    QVERIFY(b.start(jobs(2)));
    QVERIFY(!b.start(jobs(5))); // one run at a time
    QCOMPARE(b.jobCount(), 2);
    b.setMaxParallel(9); // ignored during a run
    QCOMPARE(b.maxParallel(), ConvertBatch::defaultParallel());
    // During a run, results() has an entry per job; an unfinished one is Failed with no message.
    QCOMPARE(b.results().size(), 2);
    QVERIFY(finished.wait(5000));
    QCOMPARE(w.calls.load(), 2);
    QCOMPARE(finished.size(), 1);
}

void ConvertBatchTest::destroyedWhileRunning()
{
    // The destructor stops the run and waits: no job is still working afterwards.
    Waiter w;
    w.ms = 60;
    {
        ConvertBatch b;
        b.setConverter(w.converter());
        b.setMaxParallel(2);
        QSignalSpy started(&b, &ConvertBatch::jobStarted);
        QVERIFY(b.start(jobs(20)));
        QTRY_VERIFY(started.size() >= 1);
    }
    QCOMPARE(w.now.load(), 0);
    const int calls = w.calls.load();
    QVERIFY2(calls < 20, qPrintable(QString::number(calls)));
    QTest::qWait(150);
    QCOMPARE(w.calls.load(), calls); // and none starts later
}

void ConvertBatchTest::realConversion()
{
    // With the converter left alone, the batch converts.
    QTemporaryDir dir;
    BatchOptions o;
    o.outputFolder = dir.path() + s(u"/out");
    o.settings = encoderPreset(s(u"mp3-320")).value().settings;
    const QString data = QStringLiteral(LINUXBEE_TEST_DATA "/convert/");
    const QList<ConvertJob> plan = planBatch({ data + s(u"tone.flac"), data + s(u"hires.flac") }, o);
    QVERIFY(QDir().mkpath(o.outputFolder));
    ConvertBatch b;
    QSignalSpy finished(&b, &ConvertBatch::finished);
    QVERIFY(b.start(plan));
    QVERIFY(finished.wait(60000));
    for (const ConvertResult &r : b.results())
        QVERIFY2(r.status == ConvertResult::Status::Converted, qPrintable(r.message));
    QVERIFY(QFileInfo(o.outputFolder + s(u"/tone.mp3")).size() > 1000);
    QVERIFY(QFileInfo(o.outputFolder + s(u"/hires.mp3")).size() > 1000);
}

QTEST_GUILESS_MAIN(ConvertBatchTest)
#include "tst_convertbatch.moc"
