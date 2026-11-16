// LOCKED ACCEPTANCE TEST (the background shell: the monitor's scans go through a runner). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/background-shell.md (BG-MONITOR) and
// docs/spec/scan-runner.md. Runs on the offscreen platform with a bus of its own.
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QProcess>
#include <QSettings>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QtTest>

#include "actions/actionregistry.h"
#include "desktop/mprisservice.h"
#include "library/foldermonitor.h"
#include "mainwindow/mainwindow.h"
#include "navigator/navigatorpanel.h"
#include "shell/backgroundshell.h"
#include "shell/libraryshell.h"
#include "shell/playbackshell.h"
#include "shell/scanrunner.h"

using namespace linuxbee;
using namespace linuxbee::ui;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

const QString kService = QStringLiteral("org.mpris.MediaPlayer2.linuxbee");
const QString kPath = QStringLiteral("/org/mpris/MediaPlayer2");
const int kSettle = 120;
const int kPatience = 6000;

class Tape : public AudioBackend {
    Q_OBJECT
public:
    void load(const QString &) override { }
    void play() override { }
    void pause() override { }
    void stop() override { }
    void seek(qint64) override { }
    void setVolume(double) override { }
};

struct Bus {
    QProcess daemon;
    QString address;
    bool start()
    {
        daemon.start(QStringLiteral("dbus-daemon"),
            { QStringLiteral("--session"), QStringLiteral("--nofork"), QStringLiteral("--print-address") });
        if (!daemon.waitForStarted(5000) || !daemon.waitForReadyRead(5000))
            return false;
        address = QString::fromLocal8Bit(daemon.readLine()).trimmed();
        return !address.isEmpty();
    }
    ~Bus()
    {
        daemon.kill();
        daemon.waitForFinished(3000);
    }
};
Bus *gBus = nullptr;
int gConnections = 0;

struct Rig {
    QTemporaryDir dir;
    QString a, inbox;
    std::unique_ptr<Library> library;
    PlaylistStore playlists;
    QSettings settings;
    ActionRegistry actions;
    MainWindow window;
    Tape tape;
    QDateTime now = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);
    int quits = 0;
    QString serverName, clientName;
    QDBusConnection server { QString() };
    QDBusConnection client { QString() };
    std::unique_ptr<LibraryShell> lib;
    std::unique_ptr<PlaybackShell> play;
    std::unique_ptr<BackgroundShell> background;

    Rig()
        : library(Library::open(QStringLiteral(":memory:")))
        , playlists(made(QStringLiteral("playlists")))
        , settings(dir.path() + QStringLiteral("/s.ini"), QSettings::IniFormat)
        , window(&actions, &settings)
    {
        made(QStringLiteral("music"));
        inbox = made(QStringLiteral("inbox"));
        a = put("basic.flac", "music/a.flac");
        if (library->addFile(a, QStringLiteral("Music"), now) == 0)
            QTest::qFail("a fixture did not go into the library", __FILE__, __LINE__);
        // Planted values are text, as stored values are when a later run reads them.
        settings.setValue(QStringLiteral("library/settleMs"), QString::number(kSettle));
        lib = std::make_unique<LibraryShell>(&window, &actions, library.get(), &playlists, &settings, ShellHooks { });
        play = std::make_unique<PlaybackShell>(&window, &actions, library.get(), lib.get(), &tape, &settings, [this] { return now; });
        serverName = QStringLiteral("lb-bg-server-%1").arg(++gConnections);
        clientName = QStringLiteral("lb-bg-client-%1").arg(gConnections);
        server = QDBusConnection::connectToBus(gBus->address, serverName);
        client = QDBusConnection::connectToBus(gBus->address, clientName);
    }
    ~Rig()
    {
        background.reset();
        QDBusConnection::disconnectFromBus(serverName);
        QDBusConnection::disconnectFromBus(clientName);
    }
    QString made(const QString &name)
    {
        QDir(dir.path()).mkpath(name);
        return dir.path() + QLatin1Char('/') + name;
    }
    QString put(const char *from, const char *to)
    {
        const QString target = dir.path() + QLatin1Char('/') + QLatin1String(to);
        if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/") + QLatin1String(from), target))
            QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
        return target;
    }
    void start(const QDBusConnection &bus)
    {
        BackgroundShell::Hooks hooks;
        hooks.now = [this] { return now; };
        hooks.quit = [this] { ++quits; };
        background = std::make_unique<BackgroundShell>(&window, library.get(), play.get(), &settings, hooks, bus);
    }
    void start() { start(server); }
    bool ready() const { return background && background->mpris() && background->monitor(); }
    int patience() const { return ready() && background->monitor()->settleMs() == kSettle ? kPatience : 60; }
    QDBusMessage ask(const QString &interface, const QString &method, const QVariantList &arguments = { })
    {
        QDBusMessage m = QDBusMessage::createMethodCall(kService, kPath, interface, method);
        m.setArguments(arguments);
        QDBusPendingCall pending = client.asyncCall(m, 4000);
        QDBusPendingCallWatcher watcher(pending);
        QSignalSpy done(&watcher, &QDBusPendingCallWatcher::finished);
        if (!pending.isFinished())
            done.wait(5000);
        return pending.reply();
    }
    QVariantMap metadata()
    {
        const QDBusMessage reply = ask(QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"),
            { QStringLiteral("org.mpris.MediaPlayer2.Player"), QStringLiteral("Metadata") });
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
            return { };
        const QVariant v = reply.arguments().at(0).value<QDBusVariant>().variant();
        return v.canConvert<QDBusArgument>() ? qdbus_cast<QVariantMap>(v.value<QDBusArgument>()) : v.toMap();
    }
};

#define NEED_PARTS(r) \
    do { \
        if (!(r).ready()) \
            QFAIL("the shell has not built its MPRIS service and folder monitor"); \
    } while (false)

#define NEED_RUNNER(r) \
    do { \
        NEED_PARTS(r); \
        if (!(r).background->scanner()) \
            QFAIL("the shell has no scan runner of its own"); \
    } while (false)

ScanReport reportOf(const QSignalSpy &spy, int at)
{
    return spy.at(at).at(0).value<ScanReport>();
}

}

class BackgroundScanTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void theScanIsNotInTheCall();
    void theSectionOfEachScan();
    void bothRunners();
    void goneWhileScanning();
};

void BackgroundScanTest::initTestCase()
{
    (void)&kPath;
    gBus = new Bus;
    QVERIFY2(gBus->start(), "dbus-daemon is required for this test and did not start");
}

void BackgroundScanTest::cleanupTestCase()
{
    delete gBus;
    gBus = nullptr;
}

void BackgroundScanTest::theScanIsNotInTheCall()
{
    // BG-MONITOR: the monitor's scanner asks the shell's runner and comes back; nothing is stored inside the call.
    Rig r;
    const QString one = r.put("basic.m4a", "inbox/one.m4a");
    r.settings.setValue(s(u"library/monitored"), r.inbox);
    r.start();
    NEED_RUNNER(r);
    ScanRunner *runner = r.background->scanner();
    QVERIFY(runner != r.lib->scanner()); // its own, not the window's
    QVERIFY(!runner->isRunning());
    QCOMPARE(r.library->count(), 1);

    QList<int> heldWhenAsked, addedWhenAsked;
    QList<bool> runningWhenAsked;
    QObject::connect(r.background->monitor(), &FolderMonitor::scanned, r.background.get(),
        [&](const ScanReport &report, const QStringList &) {
            heldWhenAsked.append(r.library->count());
            addedWhenAsked.append(int(report.added.size()));
            runningWhenAsked.append(runner->isRunning());
        });
    QSignalSpy started(runner, &ScanRunner::started);
    QSignalSpy finished(runner, &ScanRunner::finished);

    r.background->monitor()->checkNow();
    // Straight after the call: asked for, and nothing looked at or stored yet.
    QCOMPARE(heldWhenAsked, QList<int>({ 1 }));
    QCOMPARE(addedWhenAsked, QList<int>({ 0 })); // the monitor's report is empty now
    QCOMPARE(runningWhenAsked, QList<bool>({ true }));
    QCOMPARE(r.library->count(), 1);
    QCOMPARE(finished.count(), 0);
    QCOMPARE(r.background->lastMessage(), QString());

    QTRY_VERIFY_WITH_TIMEOUT(!runner->isRunning(), kPatience);
    QCOMPARE(started.count(), 1);
    QCOMPARE(started.at(0).at(0).toStringList(), QStringList({ r.inbox }));
    QCOMPARE(finished.count(), 1);
    QCOMPARE(reportOf(finished, 0).added.size(), 1);
    QCOMPARE(r.library->count(), 2);
    const auto track = r.library->trackByPath(one);
    QVERIFY(track.has_value());
    QCOMPARE(track->category, s(u"Inbox"));
    QCOMPARE(track->dateAdded, r.now);
    QCOMPARE(r.background->lastMessage(), s(u"Inbox: 1 new file"));
    QCOMPARE(r.window.statusBar()->currentMessage(), s(u"Inbox: 1 new file"));

    // A scan that adds nothing says nothing, when it is asked for and when it ends.
    r.window.statusBar()->clearMessage();
    r.background->monitor()->checkNow();
    QCOMPARE(heldWhenAsked.size(), 2);
    QTRY_VERIFY_WITH_TIMEOUT(!runner->isRunning(), kPatience);
    QCOMPARE(finished.count(), 2);
    QCOMPARE(r.window.statusBar()->currentMessage(), QString());
    QCOMPARE(r.background->lastMessage(), s(u"Inbox: 1 new file"));

    // The window's own Scan Folders says nothing here: that runner is another one.
    ScanOptions options;
    options.category = s(u"Music");
    options.now = r.now;
    r.put("basic.ogg", "music/b.ogg");
    r.lib->scanner()->scan({ r.dir.path() + s(u"/music") }, options);
    QTRY_VERIFY_WITH_TIMEOUT(!r.lib->scanner()->isRunning(), kPatience);
    QCOMPARE(r.library->count(), 3);
    QCOMPARE(finished.count(), 2);
    QCOMPARE(r.background->lastMessage(), s(u"Inbox: 1 new file"));
}

void BackgroundScanTest::theSectionOfEachScan()
{
    // BG-MONITOR: the section is read when the scan is asked for, and the message names that section.
    Rig r;
    const QString one = r.put("basic.m4a", "inbox/one.m4a");
    r.settings.setValue(s(u"library/monitored"), r.inbox);
    r.settings.setValue(s(u"library/monitoredCategory"), s(u"Music"));
    r.start();
    NEED_RUNNER(r);
    ScanRunner *runner = r.background->scanner();
    QStringList said;
    QObject::connect(runner, &ScanRunner::finished, r.background.get(), [&] { said.append(r.background->lastMessage()); });

    r.background->monitor()->checkNow();
    r.settings.setValue(s(u"library/monitoredCategory"), s(u"Inbox")); // changed while the scan runs
    QTRY_VERIFY_WITH_TIMEOUT(!runner->isRunning(), kPatience);
    QCOMPARE(r.library->trackByPath(one)->category, s(u"Music"));
    QCOMPARE(r.background->lastMessage(), s(u"Music: 1 new file"));

    // Two asked for one after the other, each with the section of its moment.
    const QString two = r.put("basic.ogg", "inbox/two.ogg");
    const QString three = r.put("basic.mp3", "inbox/three.mp3");
    r.background->monitor()->checkNow(); // Inbox
    r.settings.setValue(s(u"library/monitoredCategory"), s(u"Music"));
    r.background->monitor()->checkNow(); // Music: it finds nothing new by the time it stores
    QCOMPARE(runner->waiting(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(!runner->isRunning(), kPatience);
    QCOMPARE(r.library->count(), 4);
    QCOMPARE(r.library->trackByPath(two)->category, s(u"Inbox"));
    QCOMPARE(r.library->trackByPath(three)->category, s(u"Inbox"));
    QCOMPARE(r.background->lastMessage(), s(u"Inbox: 2 new files"));
    QCOMPARE(said.size(), 3);
    QCOMPARE(said.at(1), s(u"Inbox: 2 new files"));

    // Nonsense means the Inbox, as before.
    r.settings.setValue(s(u"library/monitoredCategory"), s(u"Nonsense"));
    const QString four = r.put("basic.flac", "inbox/four.flac");
    r.background->monitor()->checkNow();
    QTRY_VERIFY_WITH_TIMEOUT(!runner->isRunning(), kPatience);
    QCOMPARE(r.library->trackByPath(four)->category, s(u"Inbox"));
    QCOMPARE(r.background->lastMessage(), s(u"Inbox: 1 new file"));
}

void BackgroundScanTest::bothRunners()
{
    // scan-runner.md: two runners on one library; a file both found is stored once.
    Rig r;
    QStringList paths;
    for (int i = 0; i < 6; ++i)
        paths.append(r.put(i % 2 ? "basic.ogg" : "basic.m4a", qPrintable(QStringLiteral("inbox/f%1.%2").arg(i).arg(i % 2 ? "ogg" : "m4a"))));
    r.settings.setValue(s(u"library/monitored"), r.inbox);
    r.start();
    NEED_RUNNER(r);
    ScanRunner *mine = r.background->scanner();
    ScanRunner *windows = r.lib->scanner();
    QSignalSpy first(mine, &ScanRunner::finished);
    QSignalSpy second(windows, &ScanRunner::finished);
    ScanOptions options;
    options.category = s(u"Music");
    options.now = r.now;
    windows->scan({ r.inbox }, options);
    r.background->monitor()->checkNow();
    QVERIFY(mine->isRunning() && windows->isRunning());
    QTRY_VERIFY_WITH_TIMEOUT(!mine->isRunning() && !windows->isRunning(), kPatience);
    QCOMPARE(r.library->count(), 7);
    for (const QString &path : std::as_const(paths))
        QVERIFY2(r.library->trackByPath(path).has_value(), qPrintable(path));
    QCOMPARE(first.count(), 1);
    QCOMPARE(second.count(), 1);
    // Each file is added by one of the two and by no more than one.
    QCOMPARE(reportOf(first, 0).added.size() + reportOf(second, 0).added.size(), 6);
    // The message counts what the monitor's own scan added, which may be none.
    const int added = int(reportOf(first, 0).added.size());
    const QString expected = added == 0 ? QString() : added == 1 ? s(u"Inbox: 1 new file") : QStringLiteral("Inbox: %1 new files").arg(added);
    QCOMPARE(r.background->lastMessage(), expected);
}

void BackgroundScanTest::goneWhileScanning()
{
    // BG-BUILD: destroying the shell stops the watching and its runner; the library and the player live on.
    Rig r;
    for (int i = 0; i < 4; ++i)
        r.put("basic.m4a", qPrintable(QStringLiteral("inbox/g%1.m4a").arg(i)));
    r.settings.setValue(s(u"library/monitored"), r.inbox);
    r.start();
    NEED_RUNNER(r);
    r.background->monitor()->checkNow();
    QVERIFY(r.background->scanner()->isRunning());
    r.background.reset();
    const int held = r.library->count();
    QVERIFY(held >= 1 && held <= 5);
    QTest::qWait(3 * kSettle);
    QCOMPARE(r.library->count(), held); // nothing arrives after the shell is gone
    r.play->queue()->playNow({ r.a }, 0);
    QVERIFY(r.play->player()->state() == Player::State::Playing);
    QVERIFY(!r.client.interface()->isServiceRegistered(kService));
}

QTEST_MAIN(BackgroundScanTest)
#include "tst_ui_backgroundshell_scan.moc"
