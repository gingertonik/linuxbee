// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/background-shell.md (BG-x). Runs on
// the offscreen platform with the real library, shells, MPRIS service and
// folder monitor. The message bus is the test's own (dbus-daemon, a
// required tool here); the backend only writes down what it is told, so no
// sound is involved.
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

}

class BackgroundShellTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void structure();
    void describesFromTheLibrary();
    void raiseAndQuit();
    void withoutABus();
    void nameTaken();
    void monitorsTheInbox();
    void monitoredFolders();
    void otherSection();
    void reload();
    void checkOnStartup();
    void gone();
};

void BackgroundShellTest::initTestCase()
{
    gBus = new Bus;
    QVERIFY2(gBus->start(), "dbus-daemon is required for this test and did not start");
}

void BackgroundShellTest::cleanupTestCase()
{
    delete gBus;
    gBus = nullptr;
}

void BackgroundShellTest::structure()
{
    // BG-BUILD
    Rig r;
    r.start();
    NEED_PARTS(r);
    QVERIFY(r.background->mpris()->isRunning());
    QVERIFY(r.client.interface()->isServiceRegistered(kService));
    QCOMPARE(r.background->monitoredFolders(), QStringList()); // none unless chosen
    QCOMPARE(r.background->monitor()->watched(), QStringList());
    QCOMPARE(r.background->monitor()->settleMs(), kSettle); // from the settings
    QCOMPARE(r.background->lastMessage(), QString());
    QCOMPARE(r.library->count(), 1);
}

void BackgroundShellTest::describesFromTheLibrary()
{
    // BG-MPRIS: what the desktop is told about a track comes from the library's tags.
    Rig r;
    r.start();
    NEED_PARTS(r);
    r.play->queue()->playNow({ r.a, s(u"/not/in/the/library.flac") }, 0);
    QVariantMap m = r.metadata();
    QCOMPARE(m.value(s(u"xesam:title")).toString(), s(u"Flac Title"));
    QCOMPARE(m.value(s(u"xesam:artist")).toStringList(), QStringList({ s(u"First Artist"), s(u"Second Artist") }));
    QCOMPARE(m.value(s(u"xesam:album")).toString(), s(u"Flac Album"));
    r.play->player()->next();
    m = r.metadata();
    QCOMPARE(m.value(s(u"xesam:title")).toString(), s(u"library")); // unknown to the library: the service's own fallback
    QVERIFY(!m.contains(s(u"xesam:artist")));
    // The desktop's controls reach the same player as the window's.
    QVERIFY(r.ask(s(u"org.mpris.MediaPlayer2.Player"), s(u"Pause")).type() == QDBusMessage::ReplyMessage);
    QVERIFY(r.play->player()->state() == Player::State::Paused);
}

void BackgroundShellTest::raiseAndQuit()
{
    // BG-MPRIS
    Rig r;
    r.start();
    NEED_PARTS(r);
    r.window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&r.window));
    r.window.showMinimized();
    QTRY_VERIFY(r.window.isMinimized());
    QVERIFY(r.ask(s(u"org.mpris.MediaPlayer2"), s(u"Raise")).type() == QDBusMessage::ReplyMessage);
    QTRY_VERIFY(!r.window.isMinimized());
    QVERIFY(r.window.isVisible());
    r.window.hide();
    QVERIFY(r.ask(s(u"org.mpris.MediaPlayer2"), s(u"Raise")).type() == QDBusMessage::ReplyMessage);
    QVERIFY(r.window.isVisible());
    QCOMPARE(r.quits, 0);
    QVERIFY(r.ask(s(u"org.mpris.MediaPlayer2"), s(u"Quit")).type() == QDBusMessage::ReplyMessage);
    QCOMPARE(r.quits, 1);
}

void BackgroundShellTest::withoutABus()
{
    // BG-MPRIS: no session bus is not a reason to fail. Everything else works.
    Rig r;
    r.settings.setValue(s(u"library/monitored"), r.inbox); // one folder is stored as plain text
    r.start(QDBusConnection(s(u"no-such-connection")));
    NEED_PARTS(r);
    QVERIFY(!r.background->mpris()->isRunning());
    QVERIFY2(r.background->lastMessage().startsWith(s(u"Media keys are not available: ")), qPrintable(r.background->lastMessage()));
    QVERIFY(r.background->lastMessage().size() > 32); // a reason follows
    QCOMPARE(r.window.statusBar()->currentMessage(), r.background->lastMessage());
    QCOMPARE(r.background->monitor()->watched(), QStringList({ r.inbox }));
    r.play->queue()->playNow({ r.a }, 0);
    QVERIFY(r.play->player()->state() == Player::State::Playing);
}

void BackgroundShellTest::nameTaken()
{
    // BG-MPRIS: a second linuxBee runs without the media keys.
    Rig first;
    first.start();
    NEED_PARTS(first);
    Rig second;
    second.start();
    NEED_PARTS(second);
    QVERIFY(first.background->mpris()->isRunning());
    QVERIFY(!second.background->mpris()->isRunning());
    QVERIFY(second.background->lastMessage().startsWith(s(u"Media keys are not available: ")));
    QVERIFY(second.background->lastMessage().contains(kService));
    QCOMPARE(first.background->lastMessage(), QString());
}

void BackgroundShellTest::monitorsTheInbox()
{
    // BG-MONITOR
    Rig r;
    r.settings.setValue(s(u"library/monitored"), r.inbox); // one folder is stored as plain text
    r.start();
    NEED_PARTS(r);
    QCOMPARE(r.background->monitoredFolders(), QStringList({ r.inbox }));
    QCOMPARE(r.library->count(), 1); // starting scans nothing by itself
    const QString one = r.put("basic.m4a", "inbox/one.m4a");
    QTRY_COMPARE_WITH_TIMEOUT(r.library->count(), 2, r.patience());
    const auto track = r.library->trackByPath(one);
    QVERIFY(track.has_value());
    QCOMPARE(track->category, s(u"Inbox"));
    QCOMPARE(track->dateAdded, r.now);
    QTRY_COMPARE_WITH_TIMEOUT(r.background->lastMessage(), s(u"Inbox: 1 new file"), r.patience());
    QCOMPARE(r.window.statusBar()->currentMessage(), s(u"Inbox: 1 new file"));
    // The window shows it without being asked: the navigator's count, and the list on the Inbox node.
    QVERIFY(r.lib->navigator()->selectNode(s(u"library:Inbox")));
    QCOMPARE(r.lib->shownPaths(), QStringList({ one }));
    r.put("basic.ogg", "inbox/two.ogg");
    r.put("basic.mp3", "inbox/three.mp3");
    r.put("notaudio.mp3", "inbox/junk.mp3");
    QTRY_COMPARE_WITH_TIMEOUT(r.library->count(), 4, r.patience());
    QTRY_COMPARE_WITH_TIMEOUT(r.background->lastMessage(), s(u"Inbox: 2 new files"), r.patience());
    QCOMPARE(r.lib->shownPaths().size(), 3);
    // A scan that finds nothing new says nothing.
    r.window.statusBar()->clearMessage();
    QVERIFY(QFile::remove(r.dir.path() + s(u"/inbox/junk.mp3")));
    QSignalSpy scanned(r.background->monitor(), &FolderMonitor::scanned);
    QTRY_COMPARE_WITH_TIMEOUT(scanned.size(), 1, r.patience());
    QCOMPARE(r.window.statusBar()->currentMessage(), QString());
    QCOMPARE(r.background->lastMessage(), s(u"Inbox: 2 new files"));
}

void BackgroundShellTest::monitoredFolders()
{
    // BG-FOLDERS
    Rig r;
    r.start();
    NEED_PARTS(r);
    const QString other = r.made(s(u"other"));
    r.background->setMonitoredFolders({ r.inbox + s(u"/"), other, r.inbox, QString() });
    QCOMPARE(r.background->monitoredFolders(), QStringList({ r.inbox, other })); // as the monitor cleaned them
    QCOMPARE(r.background->monitor()->watched(), QStringList({ r.inbox, other }));
    r.settings.sync();
    QCOMPARE(r.settings.value(s(u"library/monitored")).toStringList(), QStringList({ r.inbox, other })); // stored at once
    QCOMPARE(r.library->count(), 1); // choosing folders scans nothing
    r.background->setMonitoredFolders({ });
    QCOMPARE(r.settings.value(s(u"library/monitored")).toStringList(), QStringList());
    QCOMPARE(r.background->monitor()->watched(), QStringList());
}

void BackgroundShellTest::otherSection()
{
    // BG-MONITOR: where new files go is a setting; nonsense means the Inbox.
    Rig r;
    r.settings.setValue(s(u"library/monitored"), r.inbox); // one folder is stored as plain text
    r.settings.setValue(s(u"library/monitoredCategory"), s(u"Music"));
    r.start();
    NEED_PARTS(r);
    const QString one = r.put("basic.m4a", "inbox/one.m4a");
    QTRY_COMPARE_WITH_TIMEOUT(r.library->count(), 2, r.patience());
    QCOMPARE(r.library->trackByPath(one)->category, s(u"Music"));
    QTRY_COMPARE_WITH_TIMEOUT(r.background->lastMessage(), s(u"Music: 1 new file"), r.patience());
    r.background.reset();
    r.settings.setValue(s(u"library/monitoredCategory"), s(u"Nonsense"));
    r.settings.setValue(s(u"library/settleMs"), s(u"soon")); // nonsense here means the monitor's own default
    r.start();
    NEED_PARTS(r);
    QCOMPARE(r.background->monitor()->settleMs(), 2000);
    r.background->monitor()->setSettleMs(kSettle);
    const QString two = r.put("basic.ogg", "inbox/two.ogg");
    QTRY_COMPARE_WITH_TIMEOUT(r.library->count(), 3, r.patience());
    QCOMPARE(r.library->trackByPath(two)->category, s(u"Inbox"));
}

void BackgroundShellTest::reload()
{
    // BG-RELOAD: the preferences changed the settings behind the shell's back.
    Rig r;
    const QString waiting = r.put("basic.mp3", "inbox/waiting.mp3");
    r.start();
    NEED_PARTS(r);
    r.settings.setValue(s(u"library/monitored"), r.inbox);
    r.settings.setValue(s(u"library/monitoredCategory"), s(u"Music"));
    r.settings.setValue(s(u"library/checkOnStartup"), s(u"true"));
    QCOMPARE(r.background->monitoredFolders(), QStringList()); // not until told
    r.background->reload();
    QCOMPARE(r.background->monitoredFolders(), QStringList({ r.inbox }));
    QCOMPARE(r.background->monitor()->watched(), QStringList({ r.inbox }));
    QTest::qWait(r.patience() == 60 ? 10 : 4 * kSettle);
    QCOMPARE(r.library->count(), 1); // reloading scans nothing, and this is not a start-up
    QCOMPARE(r.background->lastMessage(), QString());
    const QString one = r.put("basic.m4a", "inbox/one.m4a");
    QTRY_COMPARE_WITH_TIMEOUT(r.library->count(), 3, r.patience()); // the scan takes what was waiting too
    QCOMPARE(r.library->trackByPath(one)->category, s(u"Music")); // the section as it is now
    QCOMPARE(r.library->trackByPath(waiting)->category, s(u"Music"));
    r.settings.setValue(s(u"library/monitored"), QStringList());
    r.background->reload();
    QCOMPARE(r.background->monitor()->watched(), QStringList());
}

void BackgroundShellTest::checkOnStartup()
{
    // BG-STARTUP
    Rig r;
    const QString waiting = r.put("basic.m4a", "inbox/waiting.m4a"); // arrived while linuxBee was closed
    r.settings.setValue(s(u"library/monitored"), r.inbox); // one folder is stored as plain text
    r.start();
    NEED_PARTS(r);
    QTest::qWait(r.patience() == 60 ? 10 : 5 * kSettle);
    QCOMPARE(r.library->count(), 1); // not asked for: not checked
    r.background.reset();
    r.settings.setValue(s(u"library/checkOnStartup"), s(u"true"));
    r.start();
    NEED_PARTS(r);
    QCOMPARE(r.library->count(), 1); // not inside the constructor: the window comes up first
    QTRY_COMPARE_WITH_TIMEOUT(r.library->count(), 2, r.patience());
    QCOMPARE(r.library->trackByPath(waiting)->category, s(u"Inbox"));
    QCOMPARE(r.background->lastMessage(), s(u"Inbox: 1 new file"));
}

void BackgroundShellTest::gone()
{
    Rig r;
    r.settings.setValue(s(u"library/monitored"), r.inbox); // one folder is stored as plain text
    r.start();
    NEED_PARTS(r);
    r.background.reset();
    QVERIFY(!r.client.interface()->isServiceRegistered(kService));
    r.put("basic.m4a", "inbox/late.m4a"); // nobody is watching any more
    QTest::qWait(5 * kSettle);
    QCOMPARE(r.library->count(), 1);
    r.play->queue()->playNow({ r.a }, 0); // and the player lives on
    QVERIFY(r.play->player()->state() == Player::State::Playing);
}

QTEST_MAIN(BackgroundShellTest)
#include "tst_ui_backgroundshell.moc"
