#include "desktop/mprisservice.h"

// Stub. Plan 16 replaces every body in this file.
namespace linuxbee::ui {

struct MprisService::Private { };

MprisService::MprisService(Player *player, PlayQueue *queue, Hooks hooks, QObject *parent)
    : QObject(parent)
    , d(nullptr)
{
    Q_UNUSED(player);
    Q_UNUSED(queue);
    Q_UNUSED(hooks);
}

MprisService::~MprisService()
{
    Q_UNUSED(d);
}

QString MprisService::serviceName()
{
    return { };
}

QString MprisService::objectPath()
{
    return { };
}

bool MprisService::start(const QDBusConnection &connection, QString *error)
{
    Q_UNUSED(connection);
    if (error)
        *error = QStringLiteral("not implemented");
    return false;
}

void MprisService::stop()
{
}

bool MprisService::isRunning() const
{
    return false;
}

QVariantMap MprisService::metadata() const
{
    return { };
}

}
