#pragma once
#include <QList>
#include <QString>
#include <QStringList>

#include "library/library.h"
#include "sort/fieldsort.h"

// The artists of a set of tracks, for browsing by artist.
// Contract: docs/spec/artist-browser.md (AB-ARTIST, AB-SAME, AB-FIELD,
// AB-INDEX). Pure.
namespace linuxbee {

struct ArtistEntry {
    QString name; // as first met; empty for tracks with no artist at all
    int tracks = 0;
    int albums = 0;

    bool operator==(const ArtistEntry &other) const = default;
};

// AB-ARTIST: the artist a track is filed under.
QString browseArtist(const LibraryTrack &track);

// AB-SAME
bool sameArtist(const QString &a, const QString &b);

// AB-FIELD: the tags one can browse by, in the order offered.
QStringList browseFields();

// AB-FIELD: what a track is filed under when browsing by `field`. Never an
// empty list: a track with nothing to file it under gives one empty text.
QStringList browseValues(const LibraryTrack &track, const QString &field);

// AB-INDEX. `name` is then a value of `field`, whatever the struct is called.
QList<ArtistEntry> artistIndex(const QList<LibraryTrack> &tracks, const SortOptions &options = { },
    const QString &field = QStringLiteral("Album Artist"));

}
