#include "musicfiles.h"

// Stub. Plan 07 replaces every body in this file.
namespace linuxbee {

bool hasMusicExtension(const QString &fileName)
{
    Q_UNUSED(fileName);
    return false;
}

QStringList findMusicFiles(const QString &folder, bool recursive)
{
    Q_UNUSED(folder);
    Q_UNUSED(recursive);
    return { };
}

}
