#include "library/artistindex.h"

// Stub. Plan 11c replaces every body in this file.
namespace linuxbee {

QString browseArtist(const LibraryTrack &track)
{
    Q_UNUSED(track);
    return { };
}

bool sameArtist(const QString &a, const QString &b)
{
    Q_UNUSED(a);
    Q_UNUSED(b);
    return false;
}

QStringList browseFields()
{
    return { };
}
QStringList browseValues(const LibraryTrack &track, const QString &field)
{
    Q_UNUSED(track);
    Q_UNUSED(field);
    return { };
}
QList<ArtistEntry> artistIndex(
    const QList<LibraryTrack> &tracks, const SortOptions &options, const QString &field)
{
    Q_UNUSED(tracks);
    Q_UNUSED(options);
    Q_UNUSED(field);
    return { };
}

}
