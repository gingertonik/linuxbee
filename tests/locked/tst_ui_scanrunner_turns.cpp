// LOCKED ACCEPTANCE TEST (the scan runner stores one part a turn of the event loop). Written by the
// reviewer, not the builder. The builder makes these pass by changing src/, never by editing this
// file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/scan-runner.md (SR-TURN). Row 33 of the road: a
// 20,000-file scan held the window for up to 2,100 ms at once (job 079), because parts the looking
// had handed over waited together and were all stored in one go. Here the window's thread is held
// on purpose until the looking has handed over every part, so the backlog is certain whatever the
// machine's speed.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QTimeZone>
#include <QTimer>
#include <QtTest>

#include <atomic>

#include "library/library.h"
#include "shell/scanrunner.h"

using namespace linuxbee;
using namespace linuxbee::ui;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

const QDateTime kNow = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);

// A music folder in a temporary place; six files with a music extension, in this order:
//   a/one.mp3  a/two.flac  b/four.ogg  b/notaudio.mp3  b/three.m4a  c/five.opus
struct Folder {
    QTemporaryDir dir;
    QString root;
    Folder()
    {
        root
            = QDir::cleanPath(QFileInfo(dir.path()).canonicalFilePath()) + QStringLiteral("/music");
        for (const char16_t *sub : { u"/a", u"/b", u"/c" })
            QDir().mkpath(root + s(sub));
        put(u"basic.mp3", u"a/one.mp3");
        put(u"basic.flac", u"a/two.flac");
        put(u"basic.ogg", u"b/four.ogg");
        put(u"notaudio.mp3", u"b/notaudio.mp3");
        put(u"basic.m4a", u"b/three.m4a");
        put(u"basic.opus", u"c/five.opus");
    }
    void put(const char16_t *fixtureName, const char16_t *relative)
    {
        if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/") + s(fixtureName),
                root + QLatin1Char('/') + s(relative)))
            QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
    }
};

ScanOptions options()
{
    ScanOptions o;
    o.category = QStringLiteral("Music");
    o.now = kNow;
    return o;
}

}

class ScanRunnerTurnsTest : public QObject {
    Q_OBJECT
private slots:
    void onePartATurn();
    void finishedAfterTheLastStore();
    void cancelWhileWaiting();
    void closeWhileWaiting();
    void closeBetweenTurns();
    void aNewScanWhileWaiting();
};

namespace {

// Holds the window's thread, with no event loop, until every file is looked at and the looking
// has had time to hand over its last findings: so every part waits at once.
void holdUntilHandedOver(const std::atomic<int> &looked)
{
    for (int i = 0; i < 1000 && looked.load() < 6; ++i)
        QThread::msleep(10);
    QVERIFY(looked.load() >= 6);
    QThread::msleep(500);
}

}

void ScanRunnerTurnsTest::onePartATurn()
{
    Folder folder;
    auto library = Library::open(QStringLiteral(":memory:"));
    QVERIFY(library);
    std::atomic<int> looked { 0 };
    ScanRunnerHooks hooks;
    hooks.looking = [&looked](int, int) { ++looked; };
    ScanRunner runner(library.get(), hooks);
    runner.setPartSize(1);

    // SR-TURN: a zero timer started while a part's change signals go out fires before the next
    // part is stored. `ran` is the number of those timers that have fired.
    int parts = 0;
    int ran = 0;
    QList<int> ranBefore; // for each part after the first: the timers fired before it was stored
    QObject::connect(library.get(), &Library::tracksAdded, [&](const QList<qint64> &) {
        if (parts > 0)
            ranBefore.append(ran);
        ++parts;
        QTimer::singleShot(0, [&ran] { ++ran; });
    });
    QSignalSpy finished(&runner, &ScanRunner::finished);
    QList<int> partsAtFinished;
    QObject::connect(&runner, &ScanRunner::finished, [&] { partsAtFinished.append(parts); });

    runner.scan({ folder.root }, options());
    holdUntilHandedOver(looked);

    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
    QCOMPARE(library->count(), 5); // five audio files; with partSize 1, a part for each file
    QVERIFY2(parts >= 4, qPrintable(QStringLiteral("parts stored: %1").arg(parts)));
    QCOMPARE(ranBefore.size(), parts - 1);
    for (int i = 0; i < ranBefore.size(); ++i)
        QVERIFY2(ranBefore.at(i) >= i + 1,
            qPrintable(
                QStringLiteral("part %1 was stored before the event loop ran (timers fired: %2)")
                    .arg(i + 2)
                    .arg(ranBefore.at(i))));
    // SR-SCAN, kept: finished comes after every part is stored, with the whole scan's report.
    QCOMPARE(partsAtFinished, QList<int>({ parts }));
    QCOMPARE(finished.at(0).at(0).value<ScanReport>().added.size(), 5);
}

void ScanRunnerTurnsTest::finishedAfterTheLastStore()
{
    // SR-TURN: the looking has ended and its last findings wait behind the parts; `finished`
    // comes after the last of them is stored, with all of them in its report and the library.
    Folder folder;
    auto library = Library::open(QStringLiteral(":memory:"));
    QVERIFY(library);
    std::atomic<int> looked { 0 };
    ScanRunnerHooks hooks;
    hooks.looking = [&looked](int, int) { ++looked; };
    ScanRunner runner(library.get(), hooks);
    runner.setPartSize(1);
    int addedSignals = 0;
    int addedAfterFinished = 0;
    bool over = false;
    QObject::connect(library.get(), &Library::tracksAdded, [&](const QList<qint64> &) {
        ++addedSignals;
        if (over)
            ++addedAfterFinished;
    });
    QList<int> countAtFinished;
    QList<int> addedInReport;
    QObject::connect(&runner, &ScanRunner::finished, [&](const ScanReport &report) {
        over = true;
        countAtFinished.append(library->count());
        addedInReport.append(int(report.added.size()));
    });
    QSignalSpy idle(&runner, &ScanRunner::idle);
    runner.scan({ folder.root }, options());
    holdUntilHandedOver(looked);
    QTRY_COMPARE_WITH_TIMEOUT(idle.size(), 1, 10000);
    QTest::qWait(200);
    QCOMPARE(countAtFinished, QList<int>({ 5 }));
    QCOMPARE(addedInReport, QList<int>({ 5 }));
    QVERIFY(addedSignals >= 4);
    QCOMPARE(addedAfterFinished, 0);
}

void ScanRunnerTurnsTest::cancelWhileWaiting()
{
    // SR-STOP, kept: a part handed over before the stop took effect is stored. The stop comes
    // between two turns, after the first part, while the rest wait to be stored. All 5 are
    // stored; then `finished`, cancelled, and `idle` once. Nothing is stored after `finished`.
    Folder folder;
    auto library = Library::open(QStringLiteral(":memory:"));
    QVERIFY(library);
    std::atomic<int> looked { 0 };
    ScanRunnerHooks hooks;
    hooks.looking = [&looked](int, int) { ++looked; };
    ScanRunner runner(library.get(), hooks);
    runner.setPartSize(1);
    bool over = false;
    int parts = 0;
    int addedAfterFinished = 0;
    QObject::connect(library.get(), &Library::tracksAdded, [&](const QList<qint64> &) {
        if (over)
            ++addedAfterFinished;
        if (++parts == 1)
            QTimer::singleShot(0, &runner, [&runner] { runner.cancel(); });
    });
    QSignalSpy finished(&runner, &ScanRunner::finished);
    QObject::connect(&runner, &ScanRunner::finished, [&] { over = true; });
    QSignalSpy idle(&runner, &ScanRunner::idle);
    runner.scan({ folder.root }, options());
    holdUntilHandedOver(looked);
    QTRY_COMPARE_WITH_TIMEOUT(idle.size(), 1, 10000);
    QTest::qWait(300);
    QCOMPARE(finished.size(), 1);
    QVERIFY(finished.at(0).at(0).value<ScanReport>().cancelled);
    QCOMPARE(library->count(), 5);
    QCOMPARE(addedAfterFinished, 0);
    QCOMPARE(idle.size(), 1);
    QVERIFY(!runner.isRunning());
}

void ScanRunnerTurnsTest::closeWhileWaiting()
{
    // SR-STOP and SR-TURN: the runner destroyed (the window closed) while every part waits.
    // Nothing is stored from then on, no signal comes, and nothing breaks.
    Folder folder;
    auto library = Library::open(QStringLiteral(":memory:"));
    QVERIFY(library);
    std::atomic<int> looked { 0 };
    ScanRunnerHooks hooks;
    hooks.looking = [&looked](int, int) { ++looked; };
    auto *runner = new ScanRunner(library.get(), hooks);
    runner->setPartSize(1);
    int added = 0;
    QObject::connect(library.get(), &Library::tracksAdded, [&](const QList<qint64> &) { ++added; });
    runner->scan({ folder.root }, options());
    holdUntilHandedOver(looked);
    delete runner;
    QTest::qWait(500);
    QCOMPARE(added, 0);
    QCOMPARE(library->count(), 0);
}

void ScanRunnerTurnsTest::closeBetweenTurns()
{
    // SR-STOP and SR-TURN: the runner destroyed between two turns, after the first part, while
    // the rest wait to be stored. Nothing is stored after it, no signal comes, nothing breaks.
    Folder folder;
    auto library = Library::open(QStringLiteral(":memory:"));
    QVERIFY(library);
    std::atomic<int> looked { 0 };
    ScanRunnerHooks hooks;
    hooks.looking = [&looked](int, int) { ++looked; };
    auto *runner = new ScanRunner(library.get(), hooks);
    runner->setPartSize(1);
    bool closed = false;
    int parts = 0;
    int addedAfterClose = 0;
    QObject::connect(library.get(), &Library::tracksAdded, [&](const QList<qint64> &) {
        if (closed)
            ++addedAfterClose;
        if (++parts == 1)
            QTimer::singleShot(0, [&] {
                delete runner;
                runner = nullptr;
                closed = true;
            });
    });
    runner->scan({ folder.root }, options());
    holdUntilHandedOver(looked);
    QTRY_VERIFY_WITH_TIMEOUT(closed, 10000);
    QTest::qWait(500);
    QCOMPARE(addedAfterClose, 0);
    QVERIFY(library->count() >= 1);
}

void ScanRunnerTurnsTest::aNewScanWhileWaiting()
{
    // SR-SCAN and SR-TURN: a scan asked for while the first scan's parts wait begins after the
    // first's `finished`, which comes after its last part is stored.
    Folder first;
    Folder second;
    auto library = Library::open(QStringLiteral(":memory:"));
    QVERIFY(library);
    std::atomic<int> looked { 0 };
    ScanRunnerHooks hooks;
    hooks.looking = [&looked](int, int) { ++looked; };
    ScanRunner runner(library.get(), hooks);
    runner.setPartSize(1);
    QStringList order;
    QObject::connect(library.get(), &Library::tracksAdded, [&](const QList<qint64> &) {
        order.append(QStringLiteral("stored %1").arg(library->count()));
    });
    QObject::connect(&runner, &ScanRunner::started, [&](const QStringList &folders) {
        order.append(QStringLiteral("started ")
            + (folders == QStringList({ first.root }) ? QStringLiteral("first")
                                                      : QStringLiteral("second")));
    });
    QObject::connect(&runner, &ScanRunner::finished, [&](const ScanReport &, const QStringList &) {
        order.append(QStringLiteral("finished %1").arg(library->count()));
    });
    QSignalSpy idle(&runner, &ScanRunner::idle);
    runner.scan({ first.root }, options());
    holdUntilHandedOver(looked);
    runner.scan({ second.root }, options());
    QTRY_COMPARE_WITH_TIMEOUT(idle.size(), 1, 20000);
    QCOMPARE(library->count(), 10);
    const qsizetype firstFinished = order.indexOf(QStringLiteral("finished 5"));
    const qsizetype secondStarted = order.indexOf(QStringLiteral("started second"));
    QVERIFY2(firstFinished > 0 && secondStarted == firstFinished + 1,
        qPrintable(order.join(QStringLiteral(", "))));
}

QTEST_MAIN(ScanRunnerTurnsTest)
#include "tst_ui_scanrunner_turns.moc"
