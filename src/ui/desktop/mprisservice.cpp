#include "desktop/mprisservice.h"

#include <QDBusConnectionInterface>

#include "desktop/mprisservice_p.h"

// Being on the bus: MP-START and MP-ROOT. The requests themselves are in
// mprisadaptors.cpp.
namespace linuxbee::ui {

namespace {

// MP-START: the name and the object every desktop media control knows.
const QString kServiceName = QStringLiteral("org.mpris.MediaPlayer2.linuxbee");
const QString kObjectPath = QStringLiteral("/org/mpris/MediaPlayer2");

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

QVariantMap MprisService::metadata() const
{
    return { };
}

}
