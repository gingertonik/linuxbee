#include "app/appinfo.h"

#include <QCoreApplication>
#include <QGuiApplication>

#include "version.h"

namespace linuxbee::ui {

QString applicationDisplayName()
{
    return QStringLiteral("linuxBee");
}

QString applicationSettingsName()
{
    return QStringLiteral("linuxbee");
}

QString versionLine()
{
    return applicationDisplayName() + QLatin1Char(' ') + versionString();
}

void applyApplicationInfo(QCoreApplication &application)
{
    application.setApplicationName(applicationSettingsName());
    application.setOrganizationName(applicationSettingsName());
    application.setApplicationVersion(versionString());
    if (qobject_cast<QGuiApplication *>(&application)) {
        QGuiApplication::setApplicationDisplayName(applicationDisplayName());
        QGuiApplication::setDesktopFileName(applicationSettingsName());
    }
}

}
