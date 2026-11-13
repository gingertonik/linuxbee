#include "itunesxml.h"

// Stub. Plan 21 replaces every body in this file.
namespace linuxbee {

ImportedLibrary parseItunesLibrary(const QByteArray &xml)
{
    Q_UNUSED(xml);
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
