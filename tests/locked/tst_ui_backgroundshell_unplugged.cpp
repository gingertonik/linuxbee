// LOCKED ACCEPTANCE TEST (the folder monitor and a folder that looks unplugged, fix-m1). Written by
// the reviewer, not the builder. The builder makes these pass by changing src/, never by editing
// this file. A test believed to be wrong is reported, with the reason, and left alone.
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

class BackgroundUnpluggedTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void aWatchedFolderEmptiedIsNotGone();
    void aMountPointBelowAWatchedFolderIsNotGone();
    void aWatchedFolderThatLostAFileIsMarked();

private:
    QTemporaryDir tables;
    QString fstab;
    void writeFstab(const QString &point)
    {
        QFile file(fstab);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            QTest::qFail("could not write the table of mounts", __FILE__, __LINE__);
        if (!point.isEmpty())
            file.write(QStringLiteral("server:/share %1 nfs defaults 0 0\n").arg(point).toUtf8());
    }
};

namespace {

// The inbox holds one.m4a, two.m4a, three.m4a; the monitor watches it and checks it once.
std::unique_ptr<QSignalSpy> watching(Rig &r)
{
    for (const char *name : { "inbox/one.m4a", "inbox/two.m4a", "inbox/three.m4a" })
        r.put("basic.m4a", name);
    r.settings.setValue(s(u"library/monitored"), r.inbox);
    r.start();
    if (!r.ready() || !r.background->scanner())
        return nullptr;
    auto finished = std::make_unique<QSignalSpy>(r.background->scanner(), &ScanRunner::finished);
    r.background->monitor()->checkNow();
    if (!finished->wait(kPatience) || reportOf(*finished, 0).added.size() != 3)
        return nullptr;
    return finished;
}

int missingUnder(const Library &library, const QString &folder)
{
    int n = 0;
    for (const LibraryTrack &t : library.tracks())
        n += t.missing && t.path.startsWith(folder + QLatin1Char('/')) ? 1 : 0;
    return n;
}

}

void BackgroundUnpluggedTest::initTestCase()
{
    (void)&kPath;
    (void)&kService;
    fstab = tables.path() + QStringLiteral("/fstab");
    writeFstab(QString());
    qputenv("LINUXBEE_FSTAB", QFile::encodeName(fstab));
    gBus = new Bus;
    QVERIFY2(gBus->start(), "dbus-daemon is required");
}

void BackgroundUnpluggedTest::cleanupTestCase()
{
    delete gBus;
    gBus = nullptr;
    qunsetenv("LINUXBEE_FSTAB");
}

void BackgroundUnpluggedTest::aWatchedFolderEmptiedIsNotGone()
{
    // BG-MONITOR, L-UNPLUGGED (fix-m1): the monitor scans the folder it watches (FM-ROOTS), not
    // the folder inside it that changed. A watched folder whose disk went away (every file gone at
    // once) looks unplugged: the monitor's own scan marks none of its tracks.
    writeFstab(QString());
    Rig r;
    const std::unique_ptr<QSignalSpy> spy = watching(r);
    QVERIFY2(spy, "the monitor did not take the inbox in");
    QSignalSpy &finished = *spy;
    for (const char *name : { "/one.m4a", "/two.m4a", "/three.m4a" })
        QVERIFY(QFile::remove(r.inbox + QLatin1String(name)));
    QVERIFY2(finished.wait(kPatience) || finished.size() == 2, "the monitor did not scan");
    QCOMPARE(finished.size(), 2);
    QVERIFY2(reportOf(finished, 1).nowMissing.isEmpty(), "the monitor marked them missing");
    QCOMPARE(missingUnder(*r.library, r.inbox), 0);
}

void BackgroundUnpluggedTest::aMountPointBelowAWatchedFolderIsNotGone()
{
    // L-UNPLUGGED, rule 1: a share mounted under the watched folder goes away (the table names it;
    // nothing is mounted there now, and its files are gone with it). The monitor scans the watched
    // folder, which still holds music, and marks none of the share's tracks.
    writeFstab(QString());
    Rig r;
    const QString share = r.made(QStringLiteral("inbox/share"));
    r.put("basic.m4a", "inbox/share/four.m4a");
    r.put("basic.m4a", "inbox/share/five.m4a");
    r.settings.setValue(s(u"library/monitored"), r.inbox);
    for (const char *name : { "inbox/one.m4a", "inbox/two.m4a", "inbox/three.m4a" })
        r.put("basic.m4a", name);
    r.start();
    NEED_RUNNER(r);
    QSignalSpy done(r.background->scanner(), &ScanRunner::finished);
    r.background->monitor()->checkNow();
    QVERIFY(done.wait(kPatience));
    QCOMPARE(reportOf(done, 0).added.size(), 5);
    writeFstab(share);
    QVERIFY(QFile::remove(share + QStringLiteral("/four.m4a")));
    QVERIFY(QFile::remove(share + QStringLiteral("/five.m4a")));
    QVERIFY2(done.wait(kPatience) || done.size() == 2, "the monitor did not scan");
    QCOMPARE(done.size(), 2);
    QVERIFY2(
        reportOf(done, 1).nowMissing.isEmpty(), "the monitor marked the share's tracks missing");
    QCOMPARE(missingUnder(*r.library, share), 0);
}

void BackgroundUnpluggedTest::aWatchedFolderThatLostAFileIsMarked()
{
    // The other side: a file removed from a watched folder that still holds music is marked
    // missing by the monitor's scan, as before.
    writeFstab(QString());
    Rig r;
    const std::unique_ptr<QSignalSpy> spy = watching(r);
    QVERIFY2(spy, "the monitor did not take the inbox in");
    QSignalSpy &finished = *spy;
    QVERIFY(QFile::remove(r.inbox + QStringLiteral("/two.m4a")));
    QVERIFY2(finished.wait(kPatience) || finished.size() == 2, "the monitor did not scan");
    QCOMPARE(finished.size(), 2);
    QCOMPARE(reportOf(finished, 1).nowMissing.size(), 1);
    QCOMPARE(missingUnder(*r.library, r.inbox), 1);
}

QTEST_MAIN(BackgroundUnpluggedTest)
#include "tst_ui_backgroundshell_unplugged.moc"
