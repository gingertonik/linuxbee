#include "library/albumgroups.h"

// Stub. Plan 35 replaces every body in this file.
namespace linuxbee {

QStringList albumSortFields()
{
    return { };
}

QList<LibraryTrack> sortedForAlbums(const QList<LibraryTrack> &tracks, const SortOptions &options)
{
    Q_UNUSED(tracks);
    Q_UNUSED(options);
    return { };
}

QList<AlbumGroup> albumGroups(const QList<LibraryTrack> &tracksInOrder)
{
    Q_UNUSED(tracksInOrder);
    return { };
}

QString albumHeading(const AlbumGroup &group)
{
    Q_UNUSED(group);
    return { };
}

QString albumDetail(const AlbumGroup &group)
{
    Q_UNUSED(group);
    return { };
}

int jumpIndex(const QList<AlbumGroup> &groups, QChar letter, const SortOptions &options)
{
    Q_UNUSED(groups);
    Q_UNUSED(letter);
    Q_UNUSED(options);
    return -2;
}

}
