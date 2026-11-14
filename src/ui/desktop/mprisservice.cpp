#include "desktop/mprisservice.h"

#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QFileInfo>
#include <QUrl>

#include "desktop/mprisservice_p.h"

// Being on the bus: MP-START, MP-ROOT, MP-METADATA and MP-CHANGED. The
// requests themselves are in mprisadaptors.cpp.
namespace linuxbee::ui {

namespace {

// MP-START: the name and the object every desktop media control knows.
const QString kServiceName = QStringLiteral("org.mpris.MediaPlayer2.linuxbee");
const QString kObjectPath = QStringLiteral("/org/mpris/MediaPlayer2");

// MP-CHANGED: a change is announced over the standard Properties interface,
// naming the interface whose properties moved.
const QString kPropertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");
const QString kPlayerInterface = QStringLiteral("org.mpris.MediaPlayer2.Player");

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

// MP-CHANGED: two values of one property, compared as what they mean: a map
// entry by entry, an artist list item by item, a track id by its path.
bool sameValue(const QVariant &one, const QVariant &other)
{
    const QMetaType trackIdType = QMetaType::fromType<QDBusObjectPath>();
    if (one.metaType() == trackIdType && one.metaType() == other.metaType())
        return one.value<QDBusObjectPath>().path() == other.value<QDBusObjectPath>().path();
    if (one.typeId() == QMetaType::QVariantMap && other.typeId() == QMetaType::QVariantMap) {
        const QVariantMap a = one.toMap();
        const QVariantMap b = other.toMap();
        if (a.size() != b.size())
            return false;
        for (auto it = a.constBegin(); it != a.constEnd(); ++it) {
            const auto found = b.constFind(it.key());
            if (found == b.constEnd() || !sameValue(it.value(), found.value()))
                return false;
        }
        return true;
    }
    if (one.typeId() == QMetaType::QStringList && other.typeId() == QMetaType::QStringList)
        return one.toStringList() == other.toStringList();
    return one == other;
}

}

// MP-METADATA: the id of what is playing. With nothing playing it is the id
// MPRIS reserves for that, which is also what a track that is somehow
// nameless gets, so the id on the bus is always one the desktop was given.
QString currentTrackIdOf(const DesktopState &state)
{
    if (state.player->currentPath().isEmpty() || state.trackId.isEmpty())
        return noTrackId();
    return state.trackId;
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
    map.insert(kTrackIdKey, QVariant::fromValue(QDBusObjectPath(currentTrackIdOf(state))));
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

// MP-CHANGED: after anything the player or the queue reports, the properties
// are worked out again and the ones that moved are sent, in one signal.
// Nothing moved: nothing said. Position is never among them, and starting the
// service says nothing, because start() records the values it starts with.
void announceChanges(DesktopState &state)
{
    // MP-METADATA: a starting track reports its state, position and length
    // before Player::trackChanged gives it its id. Until then the map would
    // pair the new file with the old id, so nothing is said yet; the
    // trackChanged that follows announces all of it.
    const QString path = state.player->currentPath();
    if (!path.isEmpty() && path != state.trackIdPath)
        return;
    const QVariantMap now = state.playerAdaptor->announcedProperties();
    if (!state.running) {
        // MP-START: a service that is not running follows quietly, so that a
        // start later has something to compare against.
        state.sent = now;
        return;
    }
    QVariantMap moved;
    for (auto it = now.constBegin(); it != now.constEnd(); ++it) {
        const auto last = state.sent.constFind(it.key());
        if (last == state.sent.constEnd() || !sameValue(last.value(), it.value()))
            moved.insert(it.key(), it.value());
    }
    state.sent = now;
    if (moved.isEmpty())
        return;
    QDBusMessage signal = QDBusMessage::createSignal(
        kObjectPath, kPropertiesInterface, QStringLiteral("PropertiesChanged"));
    signal.setArguments({ QVariant(kPlayerInterface), QVariant(moved), QVariant(QStringList()) });
    state.bus.send(signal);
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
    d->state.playerAdaptor = new PlayerAdaptor(this, d->state);

    // MP-METADATA: every play is a new track to the desktop, the same file
    // played twice included, so each track that starts takes a new id.
    connect(player, &Player::trackChanged, this, [this](const QString &path) {
        if (path.isEmpty())
            d->state.trackId.clear();
        else
            d->state.trackId = newTrackId(++d->state.trackSerial);
        d->state.trackIdPath = path;
    });

    // MP-SEEK: a position report that jumps is a seek, and the desktop hears
    // it. Playing along is not: the step has to be backwards, or forwards by
    // more than 1500 ms, and a report of 0 is a track starting, not a seek.
    connect(player, &Player::positionChanged, this, [this](qint64 ms) {
        const qint64 before = d->state.lastReportMs;
        d->state.lastReportMs = ms;
        const bool playing = d->state.player->state() != Player::State::Stopped;
        if (playing && ms > 0 && (ms < before || ms - before > 1500))
            emit d->state.playerAdaptor->Seeked(ms * 1000);
    });

    // MP-CHANGED: every report of the player or the queue is a reason to work
    // the properties out again. Nothing is timed and nothing is polled: an
    // announcement is only ever a reaction to one of these.
    connect(
        player, &Player::stateChanged, this, [this](Player::State) { announceChanges(d->state); });
    connect(player, &Player::trackChanged, this,
        [this](const QString &) { announceChanges(d->state); });
    connect(player, &Player::positionChanged, this, [this](qint64) { announceChanges(d->state); });
    connect(player, &Player::durationChanged, this, [this](qint64) { announceChanges(d->state); });
    connect(player, &Player::volumeChanged, this, [this](double) { announceChanges(d->state); });
    connect(player, &Player::mutedChanged, this, [this](bool) { announceChanges(d->state); });
    connect(player, &Player::stopAfterCurrentChanged, this,
        [this](bool) { announceChanges(d->state); });
    connect(queue, &PlayQueue::entriesChanged, this, [this] { announceChanges(d->state); });
    connect(queue, &PlayQueue::currentChanged, this, [this](int) { announceChanges(d->state); });
    connect(queue, &PlayQueue::modeChanged, this, [this] { announceChanges(d->state); });
}

MprisService::~MprisService()
{
    // MP-START: going out of existence gives the name and the object back, the
    // same as stop() does, so a second linuxBee can start after this one ends.
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
    if (d->state.trackId.isEmpty() && !d->state.player->currentPath().isEmpty()) {
        d->state.trackId = newTrackId(++d->state.trackSerial);
        d->state.trackIdPath = d->state.player->currentPath();
    }
    // MP-CHANGED: starting announces nothing, so what the properties are
    // right now counts as already said.
    d->state.sent = d->state.playerAdaptor->announcedProperties();
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
