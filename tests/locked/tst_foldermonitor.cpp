// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/folder-monitor.md (FM-x). The
// monitor is watched at work on real folders in a temporary directory, with
// a scanner that only writes down what it was asked. Waits are generous;
// the settle time under test is short.
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include "library/foldermonitor.h"

using namespace linuxbee;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

const int kSettle = 120; // ms
const int kPatience = 5000;

struct World {
    QTemporaryDir dir;
    QList<QStringList> calls; // the folders of every scan asked for
    QList<qint64> sizesSeen; // size of `watchedFile` at each scan
    QString watchedFile;
    std::unique_ptr<FolderMonitor> monitor;

    World()
    {
        monitor = std::make_unique<FolderMonitor>([this](const QStringList &folders) {
            calls.append(folders);
            sizesSeen.append(watchedFile.isEmpty() ? -1 : QFileInfo(watchedFile).size());
            ScanReport report;
            report.unchanged = int(calls.size());
            return report;
        });
        monitor->setSettleMs(kSettle);
        dead = monitor->settleMs() != kSettle;
    }
    // A monitor that does not even keep its settings will not react to
    // anything: such a run fails just the same, without the long waits.
    bool dead = false;
    int patience() const { return dead ? 60 : kPatience; }
    QString path(const char *relative) const { return dir.path() + QLatin1Char('/') + QLatin1String(relative); }
    QString make(const char *relative) const
    {
        QDir().mkpath(path(relative));
        return path(relative);
    }
    void write(const char *relative, int bytes = 10, bool append = false) const
    {
        QFile f(path(relative));
        if (!f.open(append ? QIODevice::Append : QIODevice::WriteOnly))
            QTest::qFail("could not write a test file", __FILE__, __LINE__);
        f.write(QByteArray(bytes, 'x'));
    }
    bool quiet(int ms) // true when no scan is asked for during ms
    {
        const qsizetype before = calls.size();
        QTest::qWait(dead ? 10 : ms);
        return calls.size() == before;
    }
};

}

class FolderMonitorTest : public QObject {
    Q_OBJECT
private slots:
    void directories_data();
    void directories();
    void directoriesLimit();
    void roots();
    void settings();
    void watching();
    void aNewFileIsScannedOnce();
    void onlyTheRootConcerned();
    void aNewFolderIsWatched();
    void waitsForACopyToFinish();
    void checkNow();
    void foldersReplaced();
    void aFolderGoesAway();
    void gone();
    void intoTheLibrary();
};

void FolderMonitorTest::directories_data()
{
    QTest::addColumn<QStringList>("roots");
    QTest::addColumn<QStringList>("expected");
    // FM-DIRS. The tree: a, a/b, a/b/c, a/.hidden, a/.hidden/x, d, e (a file)
    QTest::newRow("one root") << QStringList { s(u"a") } << QStringList { s(u"a"), s(u"a/b"), s(u"a/b/c") };
    QTest::newRow("two roots") << QStringList { s(u"d"), s(u"a") } << QStringList { s(u"a"), s(u"a/b"), s(u"a/b/c"), s(u"d") };
    QTest::newRow("root in root") << QStringList { s(u"a"), s(u"a/b") } << QStringList { s(u"a"), s(u"a/b"), s(u"a/b/c") };
    QTest::newRow("twice") << QStringList { s(u"d"), s(u"d/"), s(u"d/../d") } << QStringList { s(u"d") };
    QTest::newRow("missing") << QStringList { s(u"nope"), s(u"d") } << QStringList { s(u"d") };
    QTest::newRow("a file") << QStringList { s(u"e") } << QStringList { };
    QTest::newRow("empty") << QStringList { QString(), s(u"  ") } << QStringList { };
    QTest::newRow("none") << QStringList { } << QStringList { };
    QTest::newRow("a hidden root is the user's choice") << QStringList { s(u"a/.hidden") }
                                                       << QStringList { s(u"a/.hidden"), s(u"a/.hidden/x") };
}

void FolderMonitorTest::directories()
{
    QFETCH(QStringList, roots);
    QFETCH(QStringList, expected);
    World w;
    w.make("a/b/c");
    w.make("a/.hidden/x");
    w.make("d");
    w.write("e");
    auto full = [&w](QStringList list) {
        for (QString &p : list)
            if (!p.trimmed().isEmpty())
                p = w.dir.path() + QLatin1Char('/') + p;
        return list;
    };
    bool limited = true;
    QCOMPARE(directoriesToWatch(full(roots), 100, &limited), full(expected));
    QVERIFY(!limited);
    QCOMPARE(directoriesToWatch(full(roots), 100), full(expected)); // the flag is optional
}

void FolderMonitorTest::directoriesLimit()
{
    // FM-DIRS: never more than the limit, and it says so.
    World w;
    for (const char *p : { "r/a", "r/b", "r/c", "r/d" })
        w.make(p);
    bool limited = false;
    QCOMPARE(directoriesToWatch({ w.path("r") }, 5, &limited).size(), 5);
    QVERIFY(!limited);
    const QStringList three = directoriesToWatch({ w.path("r") }, 3, &limited);
    QCOMPARE(three.size(), 3);
    QVERIFY(limited);
    QVERIFY(three.contains(w.path("r"))); // a root comes before what is below it
    QCOMPARE(directoriesToWatch({ w.path("r") }, 0, &limited), QStringList());
    QVERIFY(limited);
#ifdef Q_OS_UNIX
    // A link to a folder is not followed (it could lead anywhere, or round in a circle).
    QVERIFY(QFile::link(w.path("r"), w.path("r/a/loop")));
    QCOMPARE(directoriesToWatch({ w.path("r") }, 100, &limited).size(), 5);
    QVERIFY(!limited);
#endif
}

void FolderMonitorTest::roots()
{
    // FM-ROOTS
    const QStringList roots { s(u"/m/inbox"), s(u"/m/music"), s(u"/m/music/"), s(u"/other") };
    QCOMPARE(rootsFor({ s(u"/m/music/a/b") }, roots), QStringList({ s(u"/m/music") }));
    QCOMPARE(rootsFor({ s(u"/m/music") }, roots), QStringList({ s(u"/m/music") }));
    QCOMPARE(rootsFor({ s(u"/other/x"), s(u"/m/inbox/new") }, roots), QStringList({ s(u"/m/inbox"), s(u"/other") })); // order of the roots
    QCOMPARE(rootsFor({ s(u"/m/musical") }, roots), QStringList()); // a name that only starts the same
    QCOMPARE(rootsFor({ s(u"/m") }, roots), QStringList()); // above a root is not in it
    QCOMPARE(rootsFor({ }, roots), QStringList());
    QCOMPARE(rootsFor({ s(u"/m/music/a") }, { }), QStringList());
    // A root inside a root: both hold the folder.
    QCOMPARE(rootsFor({ s(u"/m/music/new/x") }, { s(u"/m/music"), s(u"/m/music/new") }),
        QStringList({ s(u"/m/music"), s(u"/m/music/new") }));
}

void FolderMonitorTest::settings()
{
    FolderMonitor m([](const QStringList &) { return ScanReport { }; });
    QCOMPARE(m.settleMs(), 2000);
    QCOMPARE(m.directoryLimit(), 4096);
    m.setSettleMs(-5);
    QCOMPARE(m.settleMs(), 0);
    m.setSettleMs(750);
    QCOMPARE(m.settleMs(), 750);
    QCOMPARE(m.folders(), QStringList());
    QCOMPARE(m.watched(), QStringList());
    QVERIFY(!m.limited());
    QVERIFY(!m.isPending());
}

void FolderMonitorTest::watching()
{
    // FM-WATCH
    World w;
    w.make("in/sub");
    w.make("music");
    w.monitor->setFolders({ w.path("in") + s(u"/"), w.path("in"), QString(), w.path("nope"), w.path("music") });
    QCOMPARE(w.monitor->folders(), QStringList({ w.path("in"), w.path("nope"), w.path("music") })); // cleaned, once each, as given
    QCOMPARE(w.monitor->watched(), QStringList({ w.path("in"), w.path("in/sub"), w.path("music") }));
    QVERIFY(!w.monitor->limited());
    QVERIFY(w.quiet(3 * kSettle)); // setting folders scans nothing
    w.monitor->setDirectoryLimit(2);
    QCOMPARE(w.monitor->watched().size(), 2);
    QVERIFY(w.monitor->limited());
    w.monitor->setDirectoryLimit(4096);
    QCOMPARE(w.monitor->watched().size(), 3);
    QVERIFY(!w.monitor->limited());
}

void FolderMonitorTest::aNewFileIsScannedOnce()
{
    // FM-SETTLE
    World w;
    w.make("in");
    w.monitor->setFolders({ w.path("in") });
    QSignalSpy scanned(w.monitor.get(), &FolderMonitor::scanned);
    w.write("in/one.flac");
    QTRY_VERIFY_WITH_TIMEOUT(w.monitor->isPending(), w.patience());
    QCOMPARE(w.calls.size(), 0); // not at once
    w.write("in/two.flac");
    w.write("in/three.flac");
    QTRY_COMPARE_WITH_TIMEOUT(w.calls.size(), 1, w.patience());
    QCOMPARE(w.calls.at(0), QStringList({ w.path("in") }));
    QCOMPARE(scanned.size(), 1);
    QCOMPARE(scanned.at(0).at(0).value<ScanReport>().unchanged, 1); // the scanner's own report is passed on
    QCOMPARE(scanned.at(0).at(1).toStringList(), QStringList({ w.path("in") }));
    QVERIFY(!w.monitor->isPending());
    QVERIFY(w.quiet(4 * kSettle)); // and only once
    // Removing a file is a change too.
    QVERIFY(QFile::remove(w.path("in/two.flac")));
    QTRY_COMPARE_WITH_TIMEOUT(w.calls.size(), 2, w.patience());
}

void FolderMonitorTest::onlyTheRootConcerned()
{
    // FM-ROOTS at work
    World w;
    w.make("in");
    w.make("music/artist/album");
    w.monitor->setFolders({ w.path("in"), w.path("music") });
    w.write("music/artist/album/track.flac");
    QTRY_COMPARE_WITH_TIMEOUT(w.calls.size(), 1, w.patience());
    QCOMPARE(w.calls.at(0), QStringList({ w.path("music") })); // the root, not the sub-folder
    w.write("in/a.flac");
    w.write("music/b.flac");
    QTRY_COMPARE_WITH_TIMEOUT(w.calls.size(), 2, w.patience());
    QCOMPARE(w.calls.at(1), QStringList({ w.path("in"), w.path("music") })); // one scan for both
    QVERIFY(w.quiet(4 * kSettle));
}

void FolderMonitorTest::aNewFolderIsWatched()
{
    // FM-WATCH: an album arrives as a new folder; what is put into it later is seen too.
    World w;
    w.make("in");
    w.monitor->setFolders({ w.path("in") });
    w.make("in/album/disc1");
    QTRY_COMPARE_WITH_TIMEOUT(w.calls.size(), 1, w.patience());
    QTRY_VERIFY_WITH_TIMEOUT(w.monitor->watched().contains(w.path("in/album/disc1")), w.patience());
    QVERIFY(w.monitor->watched().contains(w.path("in/album")));
    QVERIFY(w.quiet(3 * kSettle));
    w.write("in/album/disc1/01.flac");
    QTRY_COMPARE_WITH_TIMEOUT(w.calls.size(), 2, w.patience());
    QCOMPARE(w.calls.at(1), QStringList({ w.path("in") }));
}

void FolderMonitorTest::waitsForACopyToFinish()
{
    // FM-SETTLE: a file that is still growing is not scanned half-written.
    World w;
    w.make("in");
    w.monitor->setFolders({ w.path("in") });
    w.watchedFile = w.path("in/big.flac");
    w.write("in/big.flac", 1000);
    int writes = 0;
    QTimer writer;
    writer.setInterval(kSettle / 3);
    QObject::connect(&writer, &QTimer::timeout, &writer, [&] {
        w.write("in/big.flac", 1000, true);
        if (++writes == 15)
            writer.stop();
    });
    writer.start();
    QTRY_COMPARE_WITH_TIMEOUT(w.calls.size(), 1, w.patience());
    QCOMPARE(writes, 15); // the copy had finished
    QCOMPARE(w.sizesSeen.at(0), qint64(16000));
    QVERIFY(w.quiet(4 * kSettle));
}

void FolderMonitorTest::checkNow()
{
    // FM-CHECK
    World w;
    w.make("in");
    w.make("music");
    w.monitor->setFolders({ w.path("in"), w.path("nope"), w.path("music") });
    QSignalSpy scanned(w.monitor.get(), &FolderMonitor::scanned);
    w.monitor->checkNow();
    QCOMPARE(w.calls.size(), 1); // at once, not after a wait
    QCOMPARE(w.calls.at(0), QStringList({ w.path("in"), w.path("music") })); // roots that exist
    QCOMPARE(scanned.size(), 1);
    // It takes the place of a scan that was waiting.
    w.write("in/a.flac");
    QTRY_VERIFY_WITH_TIMEOUT(w.monitor->isPending(), w.patience());
    w.monitor->checkNow();
    QCOMPARE(w.calls.size(), 2);
    QVERIFY(!w.monitor->isPending());
    QVERIFY(w.quiet(4 * kSettle));
    // With no folder that exists there is nothing to ask for.
    w.monitor->setFolders({ w.path("nope") });
    w.monitor->checkNow();
    QCOMPARE(w.calls.size(), 2);
    FolderMonitor none(nullptr); // and with no scanner, nothing happens at all
    none.setFolders({ w.path("in") });
    none.checkNow();
}

void FolderMonitorTest::foldersReplaced()
{
    // FM-WATCH: new folders replace the old ones, and a waiting scan is dropped.
    World w;
    w.make("in");
    w.make("music");
    w.monitor->setFolders({ w.path("in") });
    w.write("in/a.flac");
    QTRY_VERIFY_WITH_TIMEOUT(w.monitor->isPending(), w.patience());
    w.monitor->setFolders({ w.path("music") });
    QVERIFY(!w.monitor->isPending());
    QCOMPARE(w.monitor->watched(), QStringList({ w.path("music") }));
    w.write("in/b.flac"); // no longer watched
    QVERIFY(w.quiet(4 * kSettle));
    w.write("music/c.flac");
    QTRY_COMPARE_WITH_TIMEOUT(w.calls.size(), 1, w.patience());
    QCOMPARE(w.calls.at(0), QStringList({ w.path("music") }));
    w.monitor->setFolders({ });
    QCOMPARE(w.monitor->watched(), QStringList());
    w.write("music/d.flac");
    QVERIFY(w.quiet(4 * kSettle));
}

void FolderMonitorTest::aFolderGoesAway()
{
    // FM-WATCH: a watched folder that is deleted is a change like any other, and is let go.
    World w;
    w.make("in/old");
    w.monitor->setFolders({ w.path("in") });
    QVERIFY(w.monitor->watched().contains(w.path("in/old")));
    QVERIFY(QDir(w.path("in/old")).removeRecursively());
    QTRY_COMPARE_WITH_TIMEOUT(w.calls.size(), 1, w.patience());
    QCOMPARE(w.calls.at(0), QStringList({ w.path("in") }));
    QTRY_COMPARE_WITH_TIMEOUT(w.monitor->watched(), QStringList({ w.path("in") }), w.patience());
}

void FolderMonitorTest::gone()
{
    // A monitor destroyed while a scan is waiting asks for nothing.
    World w;
    w.make("in");
    w.monitor->setFolders({ w.path("in") });
    w.write("in/a.flac");
    QTRY_VERIFY_WITH_TIMEOUT(w.monitor->isPending(), w.patience());
    w.monitor.reset();
    QTest::qWait(w.dead ? 10 : 4 * kSettle);
    QCOMPARE(w.calls.size(), 0);
}

void FolderMonitorTest::intoTheLibrary()
{
    // FM-LIBRARY: the scanner that goes with a library, and the two together.
    QTemporaryDir dir;
    QDir(dir.path()).mkpath(s(u"inbox"));
    const QString inbox = dir.path() + s(u"/inbox");
    auto library = Library::open(s(u":memory:"));
    QVERIFY(library);
    const QDateTime now(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);
    FolderMonitor monitor(libraryScanner(library.get(), s(u"Inbox"), [now] { return now; }));
    monitor.setSettleMs(kSettle);
    monitor.setFolders({ inbox });
    QSignalSpy scanned(&monitor, &FolderMonitor::scanned);
    QVERIFY(QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA) + s(u"/tags/basic.flac"), inbox + s(u"/new.flac")));
    QTRY_COMPARE_WITH_TIMEOUT(scanned.size(), 1, monitor.settleMs() == kSettle ? kPatience : 60);
    QCOMPARE(scanned.at(0).at(0).value<ScanReport>().added.size(), 1);
    const auto track = library->trackByPath(inbox + s(u"/new.flac"));
    QVERIFY(track.has_value());
    QCOMPARE(track->category, s(u"Inbox"));
    QCOMPARE(track->dateAdded, now);
    QCOMPARE(track->tags.first(s(u"Title")), s(u"Flac Title"));
    QCOMPARE(library->count(), 1);
}

QTEST_MAIN(FolderMonitorTest)
#include "tst_foldermonitor.moc"
