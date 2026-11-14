#include "library/artistindex.h"

#include <algorithm>

#include <QHash>
#include <QSet>

// Contract: docs/spec/artist-browser.md (AB-ARTIST, AB-SAME, AB-FIELD, AB-INDEX).
namespace linuxbee {

namespace {

// AB-SAME: two names are the same artist when their keys are equal. The key
// is trimmed and case-folded; accents are kept, so Bjork and Bjoerk differ.
QString sameKey(const QString &name)
{
    return name.trimmed().toCaseFolded();
}

bool offered(const QString &field)
{
    return browseFields().contains(field);
}

bool isAsciiDigit(QChar c)
{
    return c >= QLatin1Char('0') && c <= QLatin1Char('9');
}

// The first value of a tag that is not blank, trimmed; empty when there is none.
QString firstNonBlank(const LibraryTrack &track, const QString &field)
{
    for (const QString &value : track.tags.fields.value(field)) {
        const QString text = value.trimmed();
        if (!text.isEmpty())
            return text;
    }
    return { };
}

// AB-FIELD, Year: the first value, trimmed; its first four characters when
// those are four digits, otherwise the whole of it.
QString yearText(const LibraryTrack &track)
{
    const QStringList values = track.tags.fields.value(QStringLiteral("Year"));
    const QString text = values.isEmpty() ? QString() : values.first().trimmed();
    if (text.size() < 4)
        return text;
    const QString head = text.left(4);
    if (std::all_of(head.cbegin(), head.cend(), isAsciiDigit))
        return head;
    return text;
}

// AB-FIELD: every value of a tag that is not blank, trimmed, in the order held,
// with a value that is the same artist as an earlier one left out.
QStringList distinctValues(const LibraryTrack &track, const QString &field)
{
    QStringList out;
    QSet<QString> seen;
    for (const QString &value : track.tags.fields.value(field)) {
        const QString text = value.trimmed();
        if (text.isEmpty())
            continue;
        const QString key = sameKey(text);
        if (seen.contains(key))
            continue;
        seen << key;
        out.append(text);
    }
    return out;
}

// AB-INDEX: the album values of a track, as keys (trimmed, case folded). A
// track with no album gives the empty key, so such tracks count as one album.
QSet<QString> albumKeys(const LibraryTrack &track)
{
    QSet<QString> keys;
    for (const QString &album : track.tags.fields.value(QStringLiteral("Album"))) {
        const QString key = sameKey(album);
        if (!key.isEmpty())
            keys << key;
    }
    if (keys.isEmpty())
        keys << QString();
    return keys;
}

}

QString browseArtist(const LibraryTrack &track)
{
    const QString albumArtist = firstNonBlank(track, QStringLiteral("Album Artist"));
    if (!albumArtist.isEmpty())
        return albumArtist;
    return firstNonBlank(track, QStringLiteral("Artist"));
}

bool sameArtist(const QString &a, const QString &b)
{
    return sameKey(a) == sameKey(b);
}

QStringList browseFields()
{
    return { QStringLiteral("Album Artist"), QStringLiteral("Artist"), QStringLiteral("Album"),
        QStringLiteral("Composer"), QStringLiteral("Genre"), QStringLiteral("Grouping"),
        QStringLiteral("Year") };
}

QStringList browseValues(const LibraryTrack &track, const QString &field)
{
    if (field == QStringLiteral("Year"))
        return { yearText(track) };
    if (field == QStringLiteral("Album Artist") || !offered(field))
        return { browseArtist(track) };
    const QStringList values = distinctValues(track, field);
    return values.isEmpty() ? QStringList { QString() } : values;
}

QList<ArtistEntry> artistIndex(
    const QList<LibraryTrack> &tracks, const SortOptions &options, const QString &field)
{
    const QString tag = offered(field) ? field : QStringLiteral("Album Artist");

    struct Bucket {
        ArtistEntry entry;
        QSet<QString> albums;
    };
    QList<Bucket> buckets;
    QHash<QString, int> bucketOf; // sameKey -> index into buckets

    for (const LibraryTrack &track : tracks) {
        const QSet<QString> albums = albumKeys(track);
        for (const QString &name : browseValues(track, tag)) {
            const QString key = sameKey(name);
            int at = bucketOf.value(key, -1);
            if (at < 0) {
                at = int(buckets.size());
                bucketOf[key] = at;
                Bucket fresh;
                fresh.entry.name = name; // the spelling of the first track met
                buckets.append(fresh);
            }
            buckets[at].entry.tracks += 1;
            buckets[at].albums.unite(albums);
        }
    }

    QList<ArtistEntry> out;
    out.reserve(buckets.size());
    for (Bucket &bucket : buckets) {
        bucket.entry.albums = int(bucket.albums.size());
        out.append(bucket.entry);
    }

    // The order of the field's values; the empty name last; ties by code points.
    std::sort(out.begin(), out.end(), [&tag, &options](const ArtistEntry &a, const ArtistEntry &b) {
        const bool blankA = a.name.isEmpty();
        const bool blankB = b.name.isEmpty();
        if (blankA || blankB)
            return blankB && !blankA;
        const int order = compareFieldValues(tag, a.name, b.name, options);
        if (order != 0)
            return order < 0;
        return a.name < b.name;
    });
    return out;
}

}
