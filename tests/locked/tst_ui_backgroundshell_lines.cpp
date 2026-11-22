// LOCKED ACCEPTANCE TEST (the background shell's lines from its scan's own report). Written by the
// reviewer, not the builder. The builder makes these pass by changing src/, never by editing this
// file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/background-shell.md (BG-MONITOR), docs/spec/library.md
// (L-WAIT) and docs/spec/library-shell.md (SH-REFUSED). Runs on the offscreen platform with a bus
// of its own. The library is a file here, so another connection can hold it while the monitor
// scans.
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QElapsedTimer>
#include <QProcess>
#include <QSqlDatabase>
#include <QSqlQuery>
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
            { QStringLiteral("--session"), QStringLiteral("--nofork"),
                QStringLiteral("--print-address") });
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
        : library(Library::open(dir.path() + QStringLiteral("/library.db")))
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
        lib = std::make_unique<LibraryShell>(
            &window, &actions, library.get(), &playlists, &settings, ShellHooks { });
        play = std::make_unique<PlaybackShell>(
            &window, &actions, library.get(), lib.get(), &tape, &settings, [this] { return now; });
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
        if (!QFile::copy(
                QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/") + QLatin1String(from),
                target))
            QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
        return target;
    }
    void start(const QDBusConnection &bus)
    {
        BackgroundShell::Hooks hooks;
        hooks.now = [this] { return now; };
        hooks.quit = [this] { ++quits; };
        background = std::make_unique<BackgroundShell>(
            &window, library.get(), play.get(), &settings, hooks, bus);
    }
    void start() { start(server); }
    bool ready() const { return background && background->mpris() && background->monitor(); }
    int patience() const
    {
        return ready() && background->monitor()->settleMs() == kSettle ? kPatience : 60;
    }
    QDBusMessage ask(
        const QString &interface, const QString &method, const QVariantList &arguments = { })
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
        const QDBusMessage reply
            = ask(QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"),
                { QStringLiteral("org.mpris.MediaPlayer2.Player"), QStringLiteral("Metadata") });
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
            return { };
        const QVariant v = reply.arguments().at(0).value<QDBusVariant>().variant();
        return v.canConvert<QDBusArgument>() ? qdbus_cast<QVariantMap>(v.value<QDBusArgument>())
                                             : v.toMap();
    }
};

#define NEED_PARTS(r)                                                                              \
    do {                                                                                           \
        if (!(r).ready())                                                                          \
            QFAIL("the shell has not built its MPRIS service and folder monitor");                 \
    } while (false)

#define NEED_RUNNER(r)                                                                             \
    do {                                                                                           \
        NEED_PARTS(r);                                                                             \
        if (!(r).background->scanner())                                                            \
            QFAIL("the shell has no scan runner of its own");                                      \
    } while (false)

ScanReport reportOf(const QSignalSpy &spy, int at)
{
    return spy.at(at).at(0).value<ScanReport>();
}

// Holds the library's database for writing for as long as it lives.
struct Holder {
    QString name;
    explicit Holder(const QString &file)
        : name(QStringLiteral("holder-%1").arg(quintptr(this)))
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(file);
        if (!db.open() || !QSqlQuery(db).exec(QStringLiteral("BEGIN IMMEDIATE")))
            QTest::qFail("could not hold the database", __FILE__, __LINE__);
    }
    ~Holder()
    {
        {
            QSqlDatabase db = QSqlDatabase::database(name);
            QSqlQuery(db).exec(QStringLiteral("ROLLBACK"));
            db.close();
        }
        QSqlDatabase::removeDatabase(name);
    }
};

}

class BackgroundLinesTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void aHeldLibraryIsNotWaitedFor();
    void partOfAScanStoredThenRefused();
};

void BackgroundLinesTest::initTestCase()
{
    (void)&kPath;
    gBus = new Bus;
    QVERIFY2(gBus->start(), "dbus-daemon is required for this test and did not start");
}

void BackgroundLinesTest::cleanupTestCase()
{
    delete gBus;
    gBus = nullptr;
}

void BackgroundLinesTest::aHeldLibraryIsNotWaitedFor()
{
    // L-WAIT, BG-MONITOR: a monitor scan nobody asked for waits the quick wait for a library
    // another program holds, not the five seconds. Nothing is stored. The monitor adds no line of
    // its own; the library's notice says it, once.
    Rig r;
    r.put("basic.m4a", "inbox/one.m4a");
    r.settings.setValue(s(u"library/monitored"), r.inbox);
    r.start();
    NEED_RUNNER(r);
    // The library shell's own notice says it (job 098's probe: a second notice made here sees the
    // line already on the bar and, rightly, does not say it again). So the bar is watched.
    QSignalSpy bar(r.window.statusBar(), &QStatusBar::messageChanged);
    ScanRunner *runner = r.background->scanner();
    QSignalSpy finished(runner, &ScanRunner::finished);
    Holder holder(r.dir.path() + QStringLiteral("/library.db"));
    QElapsedTimer took;
    took.start();
    r.background->monitor()->checkNow();
    QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, kPatience);
    QVERIFY2(took.elapsed() < 3000, qPrintable(QString::number(took.elapsed())));
    QVERIFY(!reportOf(finished, 0).refused.isEmpty());
    QVERIFY(reportOf(finished, 0).added.isEmpty());
    QCOMPARE(r.background->lastMessage(), QString());
    const auto refusals = [&bar] {
        int n = 0;
        for (const QList<QVariant> &a : bar)
            n += a.at(0).toString().startsWith(QStringLiteral("The library could not be changed"))
                ? 1
                : 0;
        return n;
    };
    QTRY_VERIFY_WITH_TIMEOUT(refusals() >= 1, kPatience);
    QTest::qWait(200); // whatever is queued, said
    QCOMPARE(refusals(), 1);
    QVERIFY2(
        r.window.statusBar()->currentMessage().startsWith(s(u"The library could not be changed")),
        qPrintable(r.window.statusBar()->currentMessage()));
}

void BackgroundLinesTest::partOfAScanStoredThenRefused()
{
    // BG-MONITOR: a monitor scan stores its first part, and then another program holds the library.
    // The rest is refused. The line comes from the scan's own report: how many were added, and that
    // the rest will be found by the next scan, with the words. It is said once, whatever the order
    // in which the scan's end and the library's notice come.
    Rig r;
    for (const char *name : { "inbox/a.m4a", "inbox/b.m4a", "inbox/c.m4a", "inbox/d.m4a",
             "inbox/e.m4a", "inbox/f.m4a" })
        r.put("basic.m4a", name);
    r.settings.setValue(s(u"library/monitored"), r.inbox);
    r.start();
    NEED_RUNNER(r);
    ScanRunner *runner = r.background->scanner();
    runner->setPartSize(2);
    QSignalSpy finished(runner, &ScanRunner::finished);
    std::unique_ptr<Holder> holder;
    QObject::connect(r.library.get(), &Library::tracksAdded, r.background.get(), [&] {
        if (!holder)
            holder = std::make_unique<Holder>(r.dir.path() + QStringLiteral("/library.db"));
    });
    r.background->monitor()->checkNow();
    QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, kPatience);
    const ScanReport report = reportOf(finished, 0);
    QCOMPARE(report.added.size(), 2);
    QVERIFY(!report.refused.isEmpty());
    QTest::qWait(200); // whatever is queued, said
    const QString want = s(u"Inbox: 2 new files; the library could not be changed, and the rest "
                           u"will be found by the next scan: ")
        + report.refused;
    QCOMPARE(r.window.statusBar()->currentMessage(), want);
    QCOMPARE(r.background->lastMessage(), want);
}

QTEST_MAIN(BackgroundLinesTest)
#include "tst_ui_backgroundshell_lines.moc"
