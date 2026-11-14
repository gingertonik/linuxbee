// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Review checks for plan 16, docs/spec/desktop-mpris.md:
// - MP-METADATA: with a track, mpris:trackid is that track's own
//   /io/github/gingertonik/linuxbee/track/N, in every map the service sends,
//   announcements included. An announced map never pairs one track's id
//   with another track's file, and never carries NoTrack with a file.
// - MP-METHODS: the Player interface has the methods of the table and no
//   others (property writers are not methods on the bus).
// Same private bus arrangement as tst_ui_mpris.cpp.
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QProcess>
#include <QSignalSpy>
#include <QtTest>

#include "desktop/mprisservice.h"

using namespace linuxbee;
using linuxbee::ui::MprisService;

namespace {

const QString kRoot = QStringLiteral("org.mpris.MediaPlayer2");
const QString kPlayer = QStringLiteral("org.mpris.MediaPlayer2.Player");
const QString kProps = QStringLiteral("org.freedesktop.DBus.Properties");
const QString kService = QStringLiteral("org.mpris.MediaPlayer2.linuxbee");
const QString kPath = QStringLiteral("/org/mpris/MediaPlayer2");
const QString kNoTrack = QStringLiteral("/org/mpris/MediaPlayer2/TrackList/NoTrack");

class Tape : public AudioBackend {
    Q_OBJECT
public:
    void load(const QString &) override { }
    void play() override { }
    void pause() override { }
    void stop() override { }
    void seek(qint64) override { }
    void setVolume(double) override { }
    void lasts(qint64 ms) { emit durationChanged(ms); }
};

class Listener : public QObject {
    Q_OBJECT
public:
    QList<QVariantMap> metadata; // every Metadata announced, in order
public slots:
    void changed(const QString &interface, const QVariantMap &values, const QStringList &)
    {
        if (interface != kPlayer || !values.contains(QStringLiteral("Metadata")))
            return;
        const QVariant v = values.value(QStringLiteral("Metadata"));
        metadata.append(v.canConvert<QDBusArgument>()
                ? qdbus_cast<QVariantMap>(v.value<QDBusArgument>())
                : v.toMap());
    }
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
    PlayQueue queue;
    Tape tape;
    Player player;
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
        serverName = QStringLiteral("lbr-server-%1").arg(++gConnections);
        clientName = QStringLiteral("lbr-client-%1").arg(gConnections);
        server = QDBusConnection::connectToBus(gBus->address, serverName);
        client = QDBusConnection::connectToBus(gBus->address, clientName);
        MprisService::Hooks hooks;
        hooks.describe = [](const QString &path) {
            MprisService::Track t;
            t.title = QStringLiteral("Title of ") + QFileInfo(path).completeBaseName();
            return t;
        };
        service = std::make_unique<MprisService>(&player, &queue, hooks);
        up = service->start(server, &error);
        client.connect(kService, kPath, kProps, QStringLiteral("PropertiesChanged"), &listener,
            SLOT(changed(QString, QVariantMap, QStringList)));
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
    void settle() { ask(kProps, QStringLiteral("Get"), { kRoot, QStringLiteral("Identity") }); }
};

#define NEED_SERVICE(r)                                                                            \
    do {                                                                                           \
        if (!(r).up)                                                                               \
            QFAIL(qPrintable(QStringLiteral("the service did not start: ") + (r).error));          \
    } while (false)

QString idOf(const QVariantMap &m)
{
    return m.value(QStringLiteral("mpris:trackid")).value<QDBusObjectPath>().path();
}

}

class MprisReviewTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        gBus = new Bus;
        QVERIFY2(gBus->start(), "dbus-daemon is required for this test and did not start");
    }
    void cleanupTestCase()
    {
        delete gBus;
        gBus = nullptr;
    }
    void announcedTrackIdBelongsToItsTrack();
    void playerMethodsAreTheTable();
};

void MprisReviewTest::announcedTrackIdBelongsToItsTrack()
{
    // MP-METADATA, MP-CHANGED
    Rig r;
    NEED_SERVICE(r);
    r.queue.playNow({ QStringLiteral("/m/one.flac"), QStringLiteral("/m/two.flac"),
                        QStringLiteral("/m/three.flac") },
        0);
    r.tape.lasts(200000);
    r.player.next();
    r.tape.lasts(200000);
    r.player.next();
    r.player.previous();
    r.settle();
    QVERIFY(r.listener.metadata.size() >= 4);
    QHash<QString, QString> urlOfId; // an id names one play of one file
    for (const QVariantMap &m : std::as_const(r.listener.metadata)) {
        const QString id = idOf(m);
        const QString url = m.value(QStringLiteral("xesam:url")).toString();
        if (url.isEmpty())
            continue;
        QVERIFY2(id != kNoTrack, qPrintable(QStringLiteral("NoTrack announced with ") + url));
        QVERIFY2(id.startsWith(QStringLiteral("/io/github/gingertonik/linuxbee/track/")),
            qPrintable(id));
        QVERIFY2(!urlOfId.contains(id) || urlOfId.value(id) == url,
            qPrintable(
                QStringLiteral("%1 announced for %2 and for %3").arg(id, urlOfId.value(id), url)));
        urlOfId.insert(id, url);
    }
    QCOMPARE(urlOfId.size(), 4); // four plays, four ids
}

void MprisReviewTest::playerMethodsAreTheTable()
{
    // MP-METHODS: the writers of LoopStatus, Shuffle, Volume and Rate are
    // reached through org.freedesktop.DBus.Properties.Set only.
    Rig r;
    NEED_SERVICE(r);
    for (const char *name : { "setVolume", "setLoopStatus", "setShuffle", "setRate" }) {
        const QVariant arg = QByteArray(name) == "setVolume" || QByteArray(name) == "setRate"
            ? QVariant(0.5)
            : QByteArray(name) == "setShuffle" ? QVariant(true)
                                               : QVariant(QStringLiteral("Playlist"));
        const QDBusMessage reply = r.ask(kPlayer, QString::fromLatin1(name), { arg });
        QVERIFY2(reply.type() == QDBusMessage::ErrorMessage, name);
    }
    QCOMPARE(r.player.volume(), 1.0);
    QVERIFY(r.queue.repeat() == PlayQueue::Repeat::Off);
    QVERIFY(!r.queue.shuffle());
}

QTEST_MAIN(MprisReviewTest)
#include "tst_ui_mpris_review.moc"
