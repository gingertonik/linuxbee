#include "desktop/mprisservice.h"

#include <QDBusConnectionInterface>
#include <QDBusObjectPath>
#include <QFileInfo>
#include <QUrl>

#include "desktop/mprisservice_p.h"

// Being on the bus: MP-START, MP-ROOT and MP-METADATA. The requests
// themselves are in mprisadaptors.cpp.
namespace linuxbee::ui {

namespace {

// MP-START: the name and the object every desktop media control knows.
const QString kServiceName = QStringLiteral("org.mpris.MediaPlayer2.linuxbee");
const QString kObjectPath = QStringLiteral("/org/mpris/MediaPlayer2");

// MP-METADATA: the keys of the map, as MPRIS names them.
const QString kTrackIdKey = QStringLiteral("mpris:trackid");
const QString kUrlKey = QStringLiteral("xesam:url");
const QString kTitleKey = QStringLiteral("xesam:title");
const QString kArtistKey = QStringLiteral("xesam:artist");
const QString kAlbumKey = QStringLiteral("xesam:album");
const QString kLengthKey = QStringLiteral("mpris:length");

// MP-METADATA: what the id of a track is made of. N is new for every play, so
// the same file played twice is two tracks to a panel.
QString newTrackId(qint64 serial)
{
    return QStringLiteral("/io/github/gingertonik/linuxbee/track/%1").arg(serial);
}

// MP-METADATA: the id a player with nothing playing is expected to give.
QString noTrackId()
{
    return QStringLiteral("/org/mpris/MediaPlayer2/TrackList/NoTrack");
}

// MP-METADATA: the file as a URL, fully percent-encoded. QUrl::toString() is
// not enough: a name with a space, a hash or an accent has to be escaped.
QString fileUrl(const QString &path)
{
    return QString::fromLatin1(QUrl::fromLocalFile(path).toEncoded());
}

}

// MP-METADATA. What is not known is left out of the map, not sent empty; with
// nothing playing, the map holds the track id and nothing else.
QVariantMap metadataOf(const DesktopState &state)
{
    QVariantMap map;
    const QString path = state.player->currentPath();
    if (path.isEmpty()) {
        map.insert(kTrackIdKey, QVariant::fromValue(QDBusObjectPath(noTrackId())));
        return map;
    }
    const QString id = state.trackId.isEmpty() ? noTrackId() : state.trackId;
    map.insert(kTrackIdKey, QVariant::fromValue(QDBusObjectPath(id)));
    map.insert(kUrlKey, fileUrl(path));

    MprisService::Track known;
    if (state.hooks.describe)
        known = state.hooks.describe(path);
    map.insert(kTitleKey, known.title.isEmpty() ? QFileInfo(path).completeBaseName() : known.title);
    if (!known.artists.isEmpty())
        map.insert(kArtistKey, known.artists);
    if (!known.album.isEmpty())
        map.insert(kAlbumKey, known.album);

    // The length arrives when the audio engine reports it, and is in microseconds.
    const qint64 lengthMs = state.player->durationMs();
    if (lengthMs > 0)
        map.insert(kLengthKey, qlonglong(lengthMs) * 1000);
    return map;
}

struct MprisService::Private {
    DesktopState state;
};

MprisService::MprisService(Player *player, PlayQueue *queue, Hooks hooks, QObject *parent)
    : QObject(parent)
    , d(new Private)
{
    d->state.player = player;
    d->state.queue = queue;
    d->state.hooks = std::move(hooks);
    // The adaptors are children of this object, so registering it with
    // ExportAdaptors puts both interfaces on the bus.
    new RootAdaptor(this, d->state);
    new PlayerAdaptor(this, d->state);

    // MP-METADATA: every play is a new track to the desktop, the same file
    // played twice included, so each track that starts takes a new id.
    connect(player, &Player::trackChanged, this, [this](const QString &path) {
        if (path.isEmpty())
            d->state.trackId.clear();
        else
            d->state.trackId = newTrackId(++d->state.trackSerial);
    });
}

MprisService::~MprisService()
{
    stop();
    delete d;
}

QString MprisService::serviceName()
{
    return kServiceName;
}

QString MprisService::objectPath()
{
    return kObjectPath;
}

// MP-START: register the object, then take the name. A name that is taken
// leaves nothing behind, so the linuxBee that owns it is undisturbed.
bool MprisService::start(const QDBusConnection &connection, QString *error)
{
    if (d->state.running) {
        if (error)
            error->clear();
        return true;
    }
    if (!connection.isConnected()) {
        if (error)
            *error = QStringLiteral("There is no bus to join: the connection is not connected.");
        return false;
    }
    // QDBusConnection is a value: a copy names the same connection, and the
    // calls below are not const.
    QDBusConnection bus = connection;
    if (!bus.registerObject(kObjectPath, this, QDBusConnection::ExportAdaptors)) {
        if (error)
            *error = QStringLiteral("The object %1 could not be registered on the bus.")
                         .arg(kObjectPath);
        return false;
    }
    if (!bus.registerService(kServiceName)) {
        bus.unregisterObject(kObjectPath);
        if (error)
            *error = QStringLiteral(
                "The name %1 is already taken on this bus: another linuxBee is running.")
                         .arg(kServiceName);
        return false;
    }
    d->state.bus = bus;
    d->state.running = true;
    // MP-METADATA: a track that started before the service did has to be
    // given an id of its own, the same as one that starts while it runs.
    if (d->state.trackId.isEmpty() && !d->state.player->currentPath().isEmpty())
        d->state.trackId = newTrackId(++d->state.trackSerial);
    if (error)
        error->clear();
    return true;
}

// MP-START: the name and the object are given back. Twice is harmless.
void MprisService::stop()
{
    if (!d->state.running)
        return;
    if (d->state.bus.isConnected()) {
        d->state.bus.unregisterObject(kObjectPath);
        d->state.bus.unregisterService(kServiceName);
    }
    d->state.running = false;
}

bool MprisService::isRunning() const
{
    return d->state.running;
}

// MP-METADATA: the same map the bus is given, for the window and for tests.
QVariantMap MprisService::metadata() const
{
    return metadataOf(d->state);
}

}
