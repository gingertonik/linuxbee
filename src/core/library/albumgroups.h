#pragma once
#include <QChar>
#include <QList>
#include <QString>
#include <QStringList>

#include "library/library.h"
#include "sort/fieldsort.h"

// Tracks put into albums, for the "Album and Tracks" view.
// Contract: docs/spec/album-view.md (AV-ORDER, AV-GROUPS, AV-TEXT, AV-JUMP).
// Pure.
namespace linuxbee {

struct AlbumGroup {
    QString artist; // AB-ARTIST of the first track; empty: none
    QString album; // as the first track spells it, trimmed; empty: none
    QString year; // of the first track that has one
    QString genre; // first genre of the first track that has one
    qint64 totalMs = 0;
    QList<qint64> trackIds; // in the order given
    QStringList paths; // the same tracks, by path
    QList<int> discStarts; // positions in trackIds, above 0, where another disc begins

    bool operator==(const AlbumGroup &other) const = default;
};

// AV-ORDER: the sort keys of the view, weightiest first.
QStringList albumSortFields();

// AV-ORDER: the tracks in the order of the view. Nothing is added or dropped.
QList<LibraryTrack> sortedForAlbums(
    const QList<LibraryTrack> &tracks, const SortOptions &options = { });

// AV-GROUPS: runs of tracks that follow one another and are of one album.
QList<AlbumGroup> albumGroups(const QList<LibraryTrack> &tracksInOrder);

// AV-TEXT
QString albumHeading(const AlbumGroup &group);
QString albumDetail(const AlbumGroup &group);

// AV-JUMP: the first group whose artist begins with `letter` ('A' to 'Z'), or
// with anything else for '#'. -1: there is none.
int jumpIndex(const QList<AlbumGroup> &groups, QChar letter, const SortOptions &options = { });

}
