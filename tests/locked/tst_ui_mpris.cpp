// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
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
                t.artists = QStringList { QStringLiteral("First Artist"), QStringLiteral("Second Artist") };
            t.album = path.endsWith(QLatin1String("solo.flac")) ? QString() : QStringLiteral("An Album");
            return t;
        };
        service = std::make_unique<MprisService>(&player, &queue, hooks);
        up = service->start(server, &error);
        client.connect(kService, kPath, kProps, QStringLiteral("PropertiesChanged"), &listener,
            SLOT(changed(QString, QVariantMap, QStringList)));
        client.connect(kService, kPath, kPlayer, QStringLiteral("Seeked"), &listener, SLOT(seeked(qlonglong)));
    }
    ~Rig()
    {
        service.reset();
        QDBusConnection::disconnectFromBus(serverName);
        QDBusConnection::disconnectFromBus(clientName);
    }
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
        return ask(kProps, QStringLiteral("Set"), { kPlayer, s(property), QVariant::fromValue(QDBusVariant(value)) }).type()
            == QDBusMessage::ReplyMessage;
    }
    QVariantMap metadata()
    {
        const QVariant v = get(u"Metadata");
        return v.canConvert<QDBusArgument>() ? qdbus_cast<QVariantMap>(v.value<QDBusArgument>()) : v.toMap();
    }
    QString trackId() { return metadata().value(s(u"mpris:trackid")).value<QDBusObjectPath>().path(); }
    void settle() { ask(kProps, QStringLiteral("Get"), { kRoot, QStringLiteral("Identity") }); } // a round trip: what was sent before has arrived
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

#define NEED_SERVICE(r) \
    do { \
        if (!(r).up) \
            QFAIL(qPrintable(QStringLiteral("the service did not start: ") + (r).error)); \
    } while (false)

}

class MprisTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void names();
    void startAndStop();
    void rootInterface();
    void transport();
    void status();
    void metadata();
    void capabilities();
    void volumeLoopShuffle();
    void seeking();
    void position();
    void announcements();
    void gone();
};

void MprisTest::initTestCase()
{
    gBus = new Bus;
    QVERIFY2(gBus->start(), "dbus-daemon is required for this test and did not start");
}

void MprisTest::cleanupTestCase()
{
    delete gBus;
    gBus = nullptr;
}

void MprisTest::names()
{
    // MP-START
    QCOMPARE(MprisService::serviceName(), kService);
    QCOMPARE(MprisService::objectPath(), kPath);
}

void MprisTest::startAndStop()
{
    // MP-START
    Rig r;
    NEED_SERVICE(r);
    QVERIFY(r.service->isRunning());
    QCOMPARE(r.error, QString());
    QVERIFY(r.client.interface()->isServiceRegistered(kService));
    // A second linuxBee finds the name taken, says so, and is not running.
    {
        PlayQueue queue;
        Tape tape;
        Player player(&queue, &tape);
        QDBusConnection other = QDBusConnection::connectToBus(gBus->address, s(u"lb-second"));
        MprisService second(&player, &queue, { });
        QString why;
        QVERIFY(!second.start(other, &why));
        QVERIFY(!second.isRunning());
        QVERIFY2(why.contains(kService), qPrintable(why));
        QDBusConnection::disconnectFromBus(s(u"lb-second"));
    }
    QVERIFY(r.client.interface()->isServiceRegistered(kService)); // the first is undisturbed
    QCOMPARE(r.get(kRoot, s(u"Identity")).toString(), s(u"linuxBee"));
    r.service->stop();
    QVERIFY(!r.service->isRunning());
    QVERIFY(!r.client.interface()->isServiceRegistered(kService));
    r.service->stop(); // twice is harmless
    QVERIFY(r.service->start(r.server)); // and it can start again, with no error pointer
    QVERIFY(r.client.interface()->isServiceRegistered(kService));
    // No bus: refused with a reason, and nothing breaks.
    PlayQueue queue;
    Tape tape;
    Player player(&queue, &tape);
    MprisService lonely(&player, &queue, { });
    QString why;
    QVERIFY(!lonely.start(QDBusConnection(s(u"no-such-connection")), &why));
    QVERIFY(!why.isEmpty());
    QVERIFY(!lonely.isRunning());
    queue.playNow({ s(u"/m/one.flac") }, 0); // a service that is not running just watches
    player.stop();
}

void MprisTest::rootInterface()
{
    // MP-ROOT
    Rig r;
    NEED_SERVICE(r);
    QCOMPARE(r.get(kRoot, s(u"Identity")).toString(), s(u"linuxBee"));
    QCOMPARE(r.get(kRoot, s(u"DesktopEntry")).toString(), s(u"io.github.gingertonik.linuxbee"));
    QCOMPARE(r.get(kRoot, s(u"CanQuit")).toBool(), true);
    QCOMPARE(r.get(kRoot, s(u"CanRaise")).toBool(), true);
    QCOMPARE(r.get(kRoot, s(u"HasTrackList")), QVariant(false));
    QCOMPARE(r.get(kRoot, s(u"SupportedUriSchemes")).toStringList(), QStringList({ s(u"file") }));
    const QStringList types = r.get(kRoot, s(u"SupportedMimeTypes")).toStringList();
    QVERIFY(types.contains(s(u"audio/flac")) && types.contains(s(u"audio/mpeg")));
    QVERIFY(r.ask(kRoot, s(u"Raise")).type() == QDBusMessage::ReplyMessage);
    QCOMPARE(r.raised, 1);
    QCOMPARE(r.quits, 0);
    QVERIFY(r.ask(kRoot, s(u"Quit")).type() == QDBusMessage::ReplyMessage);
    QCOMPARE(r.quits, 1);
    QVERIFY(r.ask(kRoot, s(u"NoSuchMethod")).type() == QDBusMessage::ErrorMessage);
}

void MprisTest::transport()
{
    // MP-METHODS
    Rig r;
    NEED_SERVICE(r);
    r.queue.queueLast({ s(u"/m/one.flac"), s(u"/m/solo.flac"), s(u"/m/bare.flac") });
    QVERIFY(r.call(s(u"Play")));
    QCOMPARE(r.tape.take(), QStringList({ s(u"load /m/one.flac"), s(u"play") }));
    QVERIFY(r.call(s(u"Pause")));
    QCOMPARE(r.tape.take(), QStringList({ s(u"pause") }));
    QVERIFY(r.call(s(u"Pause"))); // already paused: nothing
    QCOMPARE(r.tape.take(), QStringList());
    QVERIFY(r.call(s(u"PlayPause")));
    QVERIFY(r.player.state() == Player::State::Playing);
    QVERIFY(r.call(s(u"PlayPause")));
    QVERIFY(r.player.state() == Player::State::Paused);
    QVERIFY(r.call(s(u"Next")));
    QCOMPARE(r.player.currentPath(), s(u"/m/solo.flac"));
    QVERIFY(r.call(s(u"Next")));
    QVERIFY(r.call(s(u"Previous")));
    QCOMPARE(r.player.currentPath(), s(u"/m/solo.flac"));
    QVERIFY(r.call(s(u"Stop")));
    QVERIFY(r.player.state() == Player::State::Stopped);
    QVERIFY(r.call(s(u"OpenUri"), { s(u"file:///m/other.flac") })); // accepted and ignored
    QVERIFY(r.player.state() == Player::State::Stopped);
    QCOMPARE(r.queue.count(), 3);
}

void MprisTest::status()
{
    // MP-PROPS
    Rig r;
    NEED_SERVICE(r);
    QCOMPARE(r.get(u"PlaybackStatus").toString(), s(u"Stopped"));
    r.queue.playNow({ s(u"/m/one.flac"), s(u"/m/solo.flac") }, 0);
    QCOMPARE(r.get(u"PlaybackStatus").toString(), s(u"Playing"));
    r.player.pause();
    QCOMPARE(r.get(u"PlaybackStatus").toString(), s(u"Paused"));
    r.player.stop();
    QCOMPARE(r.get(u"PlaybackStatus").toString(), s(u"Stopped"));
    QCOMPARE(r.get(u"Rate").toDouble(), 1.0);
    QCOMPARE(r.get(u"MinimumRate").toDouble(), 1.0);
    QCOMPARE(r.get(u"MaximumRate").toDouble(), 1.0);
    QCOMPARE(r.get(u"CanControl").toBool(), true);
    QVERIFY(r.set(u"Rate", 2.0)); // accepted, and it stays 1
    QCOMPARE(r.get(u"Rate").toDouble(), 1.0);
    // Everything at once, as a panel asks for it.
    const QDBusMessage all = r.ask(kProps, s(u"GetAll"), { kPlayer });
    QVERIFY(all.type() == QDBusMessage::ReplyMessage);
    const QVariantMap map = qdbus_cast<QVariantMap>(all.arguments().at(0).value<QDBusArgument>());
    for (const char16_t *name : { u"PlaybackStatus", u"LoopStatus", u"Rate", u"Shuffle", u"Metadata", u"Volume", u"Position",
             u"MinimumRate", u"MaximumRate", u"CanGoNext", u"CanGoPrevious", u"CanPlay", u"CanPause", u"CanSeek", u"CanControl" })
        QVERIFY2(map.contains(s(name)), qPrintable(s(name)));
}

void MprisTest::metadata()
{
    // MP-METADATA
    Rig r;
    NEED_SERVICE(r);
    QVariantMap m = r.metadata();
    QCOMPARE(m.value(s(u"mpris:trackid")).value<QDBusObjectPath>().path(), s(u"/org/mpris/MediaPlayer2/TrackList/NoTrack"));
    QCOMPARE(m.size(), 1); // nothing else when there is no track
    r.queue.playNow({ s(u"/m/one.flac"), s(u"/m/solo.flac"), s(u"/m/bare.flac") }, 0);
    m = r.metadata();
    const QString first = m.value(s(u"mpris:trackid")).value<QDBusObjectPath>().path();
    QVERIFY2(first.startsWith(s(u"/io/github/gingertonik/linuxbee/track/")), qPrintable(first));
    QCOMPARE(m.value(s(u"xesam:url")).toString(), s(u"file:///m/one.flac"));
    QCOMPARE(m.value(s(u"xesam:title")).toString(), s(u"Title of one"));
    QCOMPARE(m.value(s(u"xesam:artist")).toStringList(), QStringList({ s(u"First Artist"), s(u"Second Artist") }));
    QCOMPARE(m.value(s(u"xesam:album")).toString(), s(u"An Album"));
    QVERIFY(!m.contains(s(u"mpris:length"))); // not known yet
    r.tape.lasts(200000);
    m = r.metadata();
    QCOMPARE(m.value(s(u"mpris:length")).toLongLong(), 200000000LL); // microseconds
    QCOMPARE(m.value(s(u"mpris:length")).typeId(), int(QMetaType::LongLong));
    QCOMPARE(m.value(s(u"mpris:trackid")).value<QDBusObjectPath>().path(), first); // the same track
    r.player.next();
    m = r.metadata();
    const QString second = m.value(s(u"mpris:trackid")).value<QDBusObjectPath>().path();
    QVERIFY(second != first);
    QVERIFY(second.startsWith(s(u"/io/github/gingertonik/linuxbee/track/")));
    QCOMPARE(m.value(s(u"xesam:title")).toString(), s(u"Title of solo"));
    QVERIFY(!m.contains(s(u"xesam:artist"))); // what is not known is left out, not sent empty
    QVERIFY(!m.contains(s(u"xesam:album")));
    QVERIFY(!m.contains(s(u"mpris:length")));
    r.player.next();
    m = r.metadata();
    QCOMPARE(m.value(s(u"xesam:title")).toString(), s(u"bare")); // no title known: the file's name
    QCOMPARE(m.value(s(u"xesam:url")).toString(), s(u"file:///m/bare.flac"));
    r.player.previous(); // the same file again is a new play, with a new id
    QVERIFY(r.trackId() != second);
    QCOMPARE(r.service->metadata().value(s(u"xesam:title")).toString(), s(u"Title of solo")); // the same map, asked directly
    // A path with characters that need escaping in a URL.
    r.queue.playNow({ s(u"/m/A B/caf\u00e9 #1.flac") }, 0);
    QCOMPARE(r.metadata().value(s(u"xesam:url")).toString(), s(u"file:///m/A%20B/caf%C3%A9%20%231.flac"));
}

void MprisTest::capabilities()
{
    // MP-CAN
    Rig r;
    NEED_SERVICE(r);
    for (const char16_t *name : { u"CanGoNext", u"CanGoPrevious", u"CanPlay", u"CanPause", u"CanSeek" })
        QVERIFY2(r.get(name) == QVariant(false), qPrintable(s(name)));
    r.queue.queueLast({ s(u"/m/one.flac"), s(u"/m/solo.flac") });
    QCOMPARE(r.get(u"CanPlay").toBool(), true);
    QCOMPARE(r.get(u"CanPause").toBool(), true);
    QCOMPARE(r.get(u"CanSeek").toBool(), false);
    r.player.play();
    QCOMPARE(r.get(u"CanGoNext").toBool(), true);
    QCOMPARE(r.get(u"CanGoPrevious").toBool(), true);
    QCOMPARE(r.get(u"CanSeek").toBool(), false); // the length is not known yet
    r.tape.lasts(200000);
    QCOMPARE(r.get(u"CanSeek").toBool(), true);
    r.player.next();
    QCOMPARE(r.get(u"CanGoNext").toBool(), false); // the last track, no repeat
    r.queue.setRepeat(PlayQueue::Repeat::All);
    QCOMPARE(r.get(u"CanGoNext").toBool(), true);
    r.tape.lasts(200000);
    r.player.stop();
    QCOMPARE(r.get(u"CanSeek").toBool(), false);
    QCOMPARE(r.get(u"CanPlay").toBool(), true);
}

void MprisTest::volumeLoopShuffle()
{
    // MP-PROPS: the three that can be written
    Rig r;
    NEED_SERVICE(r);
    QCOMPARE(r.get(u"Volume").toDouble(), 1.0);
    QVERIFY(r.set(u"Volume", 0.25));
    QCOMPARE(r.player.volume(), 0.25);
    QCOMPARE(r.get(u"Volume").toDouble(), 0.25);
    QVERIFY(r.set(u"Volume", -3.0)); // below nothing is nothing
    QCOMPARE(r.player.volume(), 0.0);
    QVERIFY(r.set(u"Volume", 7.5));
    QCOMPARE(r.player.volume(), 1.0);
    r.player.setVolume(0.5); // changed in the window
    QCOMPARE(r.get(u"Volume").toDouble(), 0.5);

    QCOMPARE(r.get(u"LoopStatus").toString(), s(u"None"));
    QVERIFY(r.set(u"LoopStatus", s(u"Playlist")));
    QVERIFY(r.queue.repeat() == PlayQueue::Repeat::All);
    QVERIFY(r.set(u"LoopStatus", s(u"Track")));
    QVERIFY(r.queue.repeat() == PlayQueue::Repeat::One);
    QCOMPARE(r.get(u"LoopStatus").toString(), s(u"Track"));
    r.set(u"LoopStatus", s(u"Sideways")); // not a value: nothing changes
    QVERIFY(r.queue.repeat() == PlayQueue::Repeat::One);
    QVERIFY(r.set(u"LoopStatus", s(u"None")));
    QVERIFY(r.queue.repeat() == PlayQueue::Repeat::Off);

    QCOMPARE(r.get(u"Shuffle"), QVariant(false));
    QVERIFY(r.set(u"Shuffle", true));
    QVERIFY(r.queue.shuffle());
    QCOMPARE(r.get(u"Shuffle").toBool(), true);
    QVERIFY(r.set(u"Shuffle", false));
    QVERIFY(!r.queue.shuffle());
    // A property that is only read cannot be set.
    r.set(u"PlaybackStatus", s(u"Playing"));
    QVERIFY(r.player.state() == Player::State::Stopped);
}

void MprisTest::seeking()
{
    // MP-SEEK. All positions on the bus are microseconds.
    Rig r;
    NEED_SERVICE(r);
    r.three();
    r.tape.at(50000);
    QVERIFY(r.call(s(u"Seek"), { qlonglong(5000000) }));
    QCOMPARE(r.tape.take(), QStringList({ s(u"seek 55000") }));
    QVERIFY(r.call(s(u"Seek"), { qlonglong(-20000000) }));
    QCOMPARE(r.tape.take(), QStringList({ s(u"seek 35000") }));
    QVERIFY(r.call(s(u"Seek"), { qlonglong(-999000000) })); // before the start: the start
    QCOMPARE(r.tape.take(), QStringList({ s(u"seek 0") }));
    const QString id = r.trackId();
    QVERIFY(r.call(s(u"SetPosition"), { QVariant::fromValue(QDBusObjectPath(id)), qlonglong(120000000) }));
    QCOMPARE(r.tape.take(), QStringList({ s(u"seek 120000") }));
    // Ignored: another track's id, a place outside the track.
    QVERIFY(r.call(s(u"SetPosition"), { QVariant::fromValue(QDBusObjectPath(s(u"/io/github/gingertonik/linuxbee/track/999999"))),
                                         qlonglong(1000000) }));
    QVERIFY(r.call(s(u"SetPosition"), { QVariant::fromValue(QDBusObjectPath(id)), qlonglong(200000001) }));
    QVERIFY(r.call(s(u"SetPosition"), { QVariant::fromValue(QDBusObjectPath(id)), qlonglong(-1) }));
    QCOMPARE(r.tape.take(), QStringList());
    QVERIFY(r.call(s(u"SetPosition"), { QVariant::fromValue(QDBusObjectPath(id)), qlonglong(200000000) })); // the very end is inside
    QCOMPARE(r.tape.take(), QStringList({ s(u"seek 200000") }));
    // Past the end is "next".
    r.tape.at(190000);
    QVERIFY(r.call(s(u"Seek"), { qlonglong(30000000) }));
    QCOMPARE(r.player.currentPath(), s(u"/m/solo.flac"));
    // Nothing to seek in: stopped, or the length unknown.
    r.tape.take();
    QVERIFY(r.call(s(u"Seek"), { qlonglong(1000000) }));
    QCOMPARE(r.tape.take(), QStringList());
    r.player.stop();
    r.tape.take();
    QVERIFY(r.call(s(u"Seek"), { qlonglong(1000000) }));
    QVERIFY(r.call(s(u"SetPosition"), { QVariant::fromValue(QDBusObjectPath(r.trackId())), qlonglong(1000000) }));
    QCOMPARE(r.tape.take(), QStringList());
}

void MprisTest::position()
{
    // MP-SEEK: Position, and the Seeked signal.
    Rig r;
    NEED_SERVICE(r);
    QCOMPARE(r.get(u"Position").toLongLong(), 0LL);
    r.three();
    r.tape.at(1000);
    r.tape.at(2000);
    r.tape.at(3000);
    QCOMPARE(r.get(u"Position").toLongLong(), 3000000LL);
    QCOMPARE(r.get(u"Position").typeId(), int(QMetaType::LongLong));
    r.settle();
    QCOMPARE(r.listener.seeks, QList<qlonglong>()); // playing along is not seeking
    r.player.seek(90000); // a seek from the window
    r.settle();
    QCOMPARE(r.listener.seeks, QList<qlonglong>({ 90000000LL }));
    r.tape.at(91000);
    r.player.seek(10000); // and backwards
    r.settle();
    QCOMPARE(r.listener.seeks, QList<qlonglong>({ 90000000LL, 10000000LL }));
    r.tape.at(10500);
    r.tape.at(11400);
    r.settle();
    QCOMPARE(r.listener.seeks.size(), 2);
    // A new track starting at 0 is not a seek.
    r.player.next();
    r.tape.at(0);
    r.tape.at(900);
    r.settle();
    QCOMPARE(r.listener.seeks.size(), 2);
    // Back to the very start, asked for over the bus, is announced as 0.
    r.tape.lasts(200000);
    r.tape.at(30000);
    r.settle();
    r.listener.seeks.clear();
    QVERIFY(r.call(s(u"Seek"), { qlonglong(-90000000) }));
    r.settle();
    QCOMPARE(r.listener.seeks, QList<qlonglong>({ 0LL }));
    QVERIFY(r.call(s(u"SetPosition"), { QVariant::fromValue(QDBusObjectPath(r.trackId())), qlonglong(0) }));
    r.settle();
    QCOMPARE(r.listener.seeks, QList<qlonglong>({ 0LL, 0LL }));
    r.player.stop();
    QCOMPARE(r.get(u"Position").toLongLong(), 0LL);
}

void MprisTest::announcements()
{
    // MP-CHANGED
    Rig r;
    NEED_SERVICE(r);
    QCOMPARE(r.announced(), QVariantMap()); // starting announces nothing
    r.queue.queueLast({ s(u"/m/one.flac"), s(u"/m/solo.flac") });
    QVariantMap a = r.announced();
    QCOMPARE(a.value(s(u"CanPlay")), QVariant(true));
    QVERIFY(!a.contains(s(u"PlaybackStatus"))); // only what changed
    r.player.play();
    a = r.announced();
    QCOMPARE(a.value(s(u"PlaybackStatus")).toString(), s(u"Playing"));
    QVERIFY(a.contains(s(u"Metadata")));
    QCOMPARE(a.value(s(u"CanGoPrevious")), QVariant(true));
    QCOMPARE(qdbus_cast<QVariantMap>(a.value(s(u"Metadata")).value<QDBusArgument>()).value(s(u"xesam:title")).toString(),
        s(u"Title of one"));
    r.tape.lasts(200000);
    a = r.announced();
    QCOMPARE(a.value(s(u"CanSeek")), QVariant(true));
    QVERIFY(a.contains(s(u"Metadata"))); // the length arrived
    r.tape.at(1000);
    r.tape.at(2000);
    r.player.seek(50000);
    QCOMPARE(r.announced(), QVariantMap()); // Position is never announced
    r.player.setVolume(0.4);
    QCOMPARE(r.announced().keys(), QStringList({ s(u"Volume") }));
    r.player.setVolume(0.4);
    r.player.play(); // already playing
    QCOMPARE(r.announced(), QVariantMap()); // nothing changed, nothing said
    r.queue.setRepeat(PlayQueue::Repeat::One);
    a = r.announced();
    QCOMPARE(a.value(s(u"LoopStatus")).toString(), s(u"Track"));
    r.queue.setShuffle(true, 7);
    QCOMPARE(r.announced().value(s(u"Shuffle")), QVariant(true));
    r.player.pause();
    QCOMPARE(r.announced().value(s(u"PlaybackStatus")).toString(), s(u"Paused"));
    r.player.stop();
    a = r.announced();
    QCOMPARE(a.value(s(u"PlaybackStatus")).toString(), s(u"Stopped"));
    QCOMPARE(a.value(s(u"CanSeek")), QVariant(false));
    QCOMPARE(r.listener.otherInterfaces, QStringList()); // the root interface has nothing that changes
    // A change made over the bus is announced like any other.
    QVERIFY(r.set(u"Volume", 0.9));
    QCOMPARE(r.announced().value(s(u"Volume")).toDouble(), 0.9);
}

void MprisTest::gone()
{
    // Destroying the service gives the name back.
    Rig r;
    NEED_SERVICE(r);
    r.service.reset();
    QVERIFY(!r.client.interface()->isServiceRegistered(kService));
    r.queue.playNow({ s(u"/m/one.flac") }, 0); // and nothing dangling listens
    r.tape.at(5000);
    r.player.stop();
    QVERIFY(r.ask(kPlayer, s(u"Play")).type() == QDBusMessage::ErrorMessage);
}

QTEST_MAIN(MprisTest)
#include "tst_ui_mpris.moc"
