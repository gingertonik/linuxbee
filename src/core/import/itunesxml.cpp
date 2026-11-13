#include "itunesxml.h"

// Stub. Plan 21 replaces every body in this file.
namespace linuxbee {

ImportedLibrary parseItunesLibrary(const QByteArray &xml)
{
    Q_UNUSED(xml);
    return { };
}

QString pathFromItunesLocation(const QString &location)
{
    Q_UNUSED(location);
    return { };
}

QString mapImportedPath(const QString &path, const PathMapping &mapping)
{
    Q_UNUSED(path);
    Q_UNUSED(mapping);
    return { };
}

ImportPlan planImport(const ImportedLibrary &imported, const QList<LibraryTrack> &libraryTracks,
    const PathMapping &mapping)
{
    Q_UNUSED(imported);
    Q_UNUSED(libraryTracks);
    Q_UNUSED(mapping);
    return { };
}

}
