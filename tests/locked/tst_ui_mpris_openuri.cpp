// LOCKED ACCEPTANCE TEST (MPRIS OpenUri plays the file it is handed). Written by the reviewer, not
// the builder. The builder makes these pass by changing src/, never by editing this file. A test
// believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/desktop-mpris.md (MP-x), which
// follow the MPRIS D-Bus Interface Specification 2.2.
//
// The test starts a message bus of its own (dbus-daemon, a required tool
// here), so the desktop's real bus is never touched. The service is on one
// connection and the test talks to it over another, as a panel widget
// would. The real player and queue are used, over a backend that only
// writes down what it is told. No sound is involved.
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QUrl>
#include <QProcess>
#include <QSignalSpy>
#include <QtTest>

#include "desktop/mprisservice.h"

using namespace linuxbee;
using linuxbee::ui::MprisService;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

const QString kRoot = QStringLiteral("org.mpris.MediaPlayer2");
const QString kPlayer = QStringLiteral("org.mpris.MediaPlayer2.Player");
const QString kProps = QStringLiteral("org.freedesktop.DBus.Properties");
const QString kService = QStringLiteral("org.mpris.MediaPlayer2.linuxbee");
const QString kPath = QStringLiteral("/org/mpris/MediaPlayer2");

class Tape : public AudioBackend {
    Q_OBJECT
public:
    QStringList log;
    void load(const QString &path) override { log.append(QStringLiteral("load ") + path); }
    void play() override { log.append(QStringLiteral("play")); }
    void pause() override { log.append(QStringLiteral("pause")); }
    void stop() override { log.append(QStringLiteral("stop")); }
    void seek(qint64 ms) override { log.append(QStringLiteral("seek %1").arg(ms)); }
    void setVolume(double) override { }
    void at(qint64 ms) { emit positionChanged(ms); }
    void lasts(qint64 ms) { emit durationChanged(ms); }
    QStringList take()
    {
        const QStringList out = log;
        log.clear();
        return out;
    }
};

// What arrives at the other end of the bus.
class Listener : public QObject {
    Q_OBJECT
public:
    QList<QVariantMap> changes; // one map per PropertiesChanged of the Player interface
    QStringList otherInterfaces;
    QList<qlonglong> seeks;
public slots:
    void changed(const QString &interface, const QVariantMap &values, const QStringList &)
    {
        if (interface == kPlayer)
            changes.append(values);
        else
            otherInterfaces.append(interface);
    }
    void seeked(qlonglong position) { seeks.append(position); }
};

// One private bus for the whole test program.
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
    PlayQueue queue;
    Tape tape;
    Player player;
    int raised = 0;
    int quits = 0;
    QString serverName;
    QString clientName;
    QDBusConnection server { QString() };
    QDBusConnection client { QString() };
    std::unique_ptr<MprisService> service;
    Listener listener;
    QString error;
    bool up = false;

    Rig()
        : player(&queue, &tape)
    {
        serverName = QStringLiteral("lb-server-%1").arg(++gConnections);
        clientName = QStringLiteral("lb-client-%1").arg(gConnections);
        server = QDBusConnection::connectToBus(gBus->address, serverName);
        client = QDBusConnection::connectToBus(gBus->address, clientName);
        MprisService::Hooks hooks;
        hooks.raise = [this] { ++raised; };
        hooks.quit = [this] { ++quits; };
        hooks.describe = [](const QString &path) {
            MprisService::Track t;
            if (path.endsWith(QLatin1String("bare.flac")))
                return t;
            t.title = QStringLiteral("Title of ") + QFileInfo(path).completeBaseName();
            if (!path.endsWith(QLatin1String("solo.flac")))
                t.artists = QStringList { QStringLiteral("First Artist"),
                    QStringLiteral("Second Artist") };
            t.album = path.endsWith(QLatin1String("solo.flac")) ? QString()
                                                                : QStringLiteral("An Album");
            return t;
        };
        service = std::make_unique<MprisService>(&player, &queue, hooks);
        up = service->start(server, &error);
        client.connect(kService, kPath, kProps, QStringLiteral("PropertiesChanged"), &listener,
            SLOT(changed(QString, QVariantMap, QStringList)));
        client.connect(
            kService, kPath, kPlayer, QStringLiteral("Seeked"), &listener, SLOT(seeked(qlonglong)));
    }
    ~Rig()
    {
        service.reset();
        QDBusConnection::disconnectFromBus(serverName);
        QDBusConnection::disconnectFromBus(clientName);
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
    bool call(const QString &method, const QVariantList &arguments = { })
    {
        return ask(kPlayer, method, arguments).type() == QDBusMessage::ReplyMessage;
    }
    QVariant get(const QString &interface, const QString &property)
    {
        const QDBusMessage reply = ask(kProps, QStringLiteral("Get"), { interface, property });
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
            return QVariant(QStringLiteral("NO ANSWER: ") + reply.errorMessage());
        return reply.arguments().at(0).value<QDBusVariant>().variant();
    }
    QVariant get(const char16_t *property) { return get(kPlayer, s(property)); }
    bool set(const char16_t *property, const QVariant &value)
    {
        return ask(kProps, QStringLiteral("Set"),
                   { kPlayer, s(property), QVariant::fromValue(QDBusVariant(value)) })
                   .type()
            == QDBusMessage::ReplyMessage;
    }
    QVariantMap metadata()
    {
        const QVariant v = get(u"Metadata");
        return v.canConvert<QDBusArgument>() ? qdbus_cast<QVariantMap>(v.value<QDBusArgument>())
                                             : v.toMap();
    }
    QString trackId()
    {
        return metadata().value(s(u"mpris:trackid")).value<QDBusObjectPath>().path();
    }
    void settle()
    {
        ask(kProps, QStringLiteral("Get"), { kRoot, QStringLiteral("Identity") });
    } // a round trip: what was sent before has arrived
    // Every property announced since the last call, later values winning.
    QVariantMap announced()
    {
        settle();
        QVariantMap all;
        for (const QVariantMap &one : listener.changes)
            for (auto it = one.begin(); it != one.end(); ++it)
                all.insert(it.key(), it.value());
        listener.changes.clear();
        return all;
    }
    void three() // three tracks, the first playing, 200 seconds long
    {
        queue.playNow({ s(u"/m/one.flac"), s(u"/m/solo.flac"), s(u"/m/bare.flac") }, 0);
        tape.lasts(200000);
        tape.take();
        announced();
        listener.seeks.clear();
    }
};

#define NEED_SERVICE(r)                                                                            \
    do {                                                                                           \
        if (!(r).up)                                                                               \
            QFAIL(qPrintable(QStringLiteral("the service did not start: ") + (r).error));          \
    } while (false)

}

class MprisOpenUriTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void playsTheFileItIsHanded();
    void startsWhenStopped();
    void leavesAloneWhatItCannotOpen();

private:
    QTemporaryDir dir;
    QString made(const QString &name);
};

void MprisOpenUriTest::initTestCase()
{
    gBus = new Bus;
    QVERIFY2(gBus->start(), "dbus-daemon is required for this test and did not start");
}

void MprisOpenUriTest::cleanupTestCase()
{
    delete gBus;
    gBus = nullptr;
}

QString MprisOpenUriTest::made(const QString &name)
{
    const QString path = dir.path() + QLatin1Char('/') + name;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write("not played here") < 0)
        QTest::qFail("could not make a file", __FILE__, __LINE__);
    return path;
}

void MprisOpenUriTest::playsTheFileItIsHanded()
{
    // MP-METHODS: OpenUri with a file the desktop hands over plays it now: the queue is that file
    // alone (PlayQueue::playNow), and it starts (X-QUEUE). The URL is percent-encoded.
    Rig r;
    NEED_SERVICE(r);
    r.three();
    const QString path = made(s(u"A B #1.flac"));
    QVERIFY(r.call(s(u"OpenUri"), { QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded) }));
    QCOMPARE(r.queue.count(), 1);
    QCOMPARE(r.queue.currentPath(), path);
    QCOMPARE(r.player.currentPath(), path);
    QVERIFY(r.player.state() == Player::State::Playing);
}

void MprisOpenUriTest::startsWhenStopped()
{
    // MPRIS: "If the playback is stopped, starts playing."
    Rig r;
    NEED_SERVICE(r);
    QVERIFY(r.player.state() == Player::State::Stopped);
    const QString path = made(s(u"one.flac"));
    QVERIFY(r.call(s(u"OpenUri"), { QUrl::fromLocalFile(path).toString() }));
    QCOMPARE(r.queue.count(), 1);
    QVERIFY(r.player.state() == Player::State::Playing);
    QCOMPARE(r.player.currentPath(), path);
}

void MprisOpenUriTest::leavesAloneWhatItCannotOpen()
{
    // MP-METHODS: another scheme, a file that is not there, a folder, a name with no scheme and an
    // empty text are accepted and change nothing; what plays plays on.
    Rig r;
    NEED_SERVICE(r);
    r.three();
    const QString kept = made(s(u"there.flac"));
    QVERIFY(kept.startsWith(QLatin1Char('/')));
    const QStringList uris {
        s(u"http://example.invalid") + kept, // a real file's path, by another scheme
        QUrl::fromLocalFile(dir.path() + s(u"/missing.flac")).toString(),
        QUrl::fromLocalFile(dir.path()).toString(),
        kept,
        QString(),
    };
    for (const QString &uri : uris) {
        QVERIFY2(r.call(s(u"OpenUri"), { uri }), qPrintable(uri));
        QCOMPARE(r.queue.count(), 3);
        QCOMPARE(r.player.currentPath(), s(u"/m/one.flac"));
        QVERIFY(r.player.state() == Player::State::Playing);
    }
}

QTEST_MAIN(MprisOpenUriTest)
#include "tst_ui_mpris_openuri.moc"
