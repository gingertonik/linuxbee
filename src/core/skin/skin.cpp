#include "skin/skin.h"

// Stub. Plan 23 replaces every body in this file.
namespace linuxbee {

Skin readSkin(const QByteArray &bytes)
{
    Q_UNUSED(bytes);
    return { };
}

Skin layeredSkin(const Skin &base, const Skin &top)
{
    Q_UNUSED(base);
    Q_UNUSED(top);
    return { };
}

QMap<QString, SkinColour> skinColours(const Skin &skin)
{
    Q_UNUSED(skin);
    return { };
}

QString skinColourText(const SkinColour &colour)
{
    Q_UNUSED(colour);
    return { };
}

QString skinStyleSheet(const QMap<QString, SkinColour> &colours)
{
    Q_UNUSED(colours);
    return QStringLiteral("?");
}

}
