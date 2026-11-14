#include "settingsimport/settingsimport.h"

// Stub. Plan 22 replaces every body in this file.
namespace linuxbee {

MusicBeeSettings readMusicBeeSettings(const QByteArray &bytes)
{
    Q_UNUSED(bytes);
    return { };
}

QString hotkeyText(int storedKey)
{
    Q_UNUSED(storedKey);
    return QStringLiteral("?");
}

QString templateFromNamingRule(const QList<MusicBeeNamingPart> &parts)
{
    Q_UNUSED(parts);
    return QStringLiteral("?");
}

QList<SettingsImportLine> planSettingsImport(const MusicBeeSettings &settings)
{
    Q_UNUSED(settings);
    return { };
}

int applySettingsImport(const QList<SettingsImportLine> &plan, QSettings &target)
{
    Q_UNUSED(plan);
    Q_UNUSED(target);
    return -1;
}

}
