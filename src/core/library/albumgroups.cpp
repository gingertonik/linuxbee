#include "library/albumgroups.h"

#include <algorithm>
#include <iterator>

#include <QHash>
#include <QMap>

#include "library/artistindex.h"
#include "search/search.h"

// Contract: docs/spec/album-view.md (AV-ORDER, AV-GROUPS, AV-TEXT, AV-JUMP).
namespace linuxbee {

namespace {

// The first value of a tag, trimmed; empty when the track has none.
QString firstValue(const LibraryTrack &track, const QString &field)
{
    return track.tags.fields.value(field).value(0).trimmed();
}

// AV-ORDER: the text a track is compared by for one sort key.
QString sortKey(const LibraryTrack &track, const LibraryFieldSource &source, const QString &field)
{
    if (field == QStringLiteral("Album Artist"))
        return browseArtist(track);
    if (field == QStringLiteral("Year"))
        return browseValues(track, field).value(0);
    return source.value(field).value_or(QString());
}

// AV-GROUPS: two Album values are the same when equal trimmed, case ignored.
bool sameAlbum(const QString &a, const QString &b)
{
    return a.trimmed().toCaseFolded() == b.trimmed().toCaseFolded();
}

bool isLatinCapital(QChar c)
{
    return c >= QLatin1Char('A') && c <= QLatin1Char('Z');
}

// AV-JUMP: the character an artist is filed under on the jump bar, upper
// case, or the null character when the artist is blank.
QChar jumpCharacter(const QString &artist, const SortOptions &options)
{
    QString text = foldForSearch(artist).trimmed();
    // One leading ignored word, and the spaces after it, are dropped: but
    // only when something follows ("The" alone stays, "Theory" is no match).
    for (const QString &word : options.ignoreWords) {
        const QString folded = foldForSearch(word).trimmed();
        if (folded.isEmpty() || !text.startsWith(folded + QLatin1Char(' ')))
            continue;
        const QString rest = text.mid(folded.size() + 1).trimmed();
        if (!rest.isEmpty()) {
            text = rest;
            break;
        }
    }
    return text.isEmpty() ? QChar() : text.at(0).toUpper();
}

}

QStringList albumSortFields()
{
    return { QStringLiteral("Album Artist"), QStringLiteral("Year"), QStringLiteral("Album"),
        QStringLiteral("Disc#"), QStringLiteral("Track#"), QStringLiteral("Title") };
}

QList<LibraryTrack> sortedForAlbums(const QList<LibraryTrack> &tracks, const SortOptions &options)
{
    const QStringList fields = albumSortFields();

    // The key texts are worked out, and prepared, once per track, not once per
    // comparison (T-PREPARED).
    struct Keyed {
        qsizetype at;
        QList<PreparedValue> keys;
    };
    QList<Keyed> keyed;
    keyed.reserve(tracks.size());
    for (qsizetype i = 0; i < tracks.size(); ++i) {
        const LibraryFieldSource source(tracks.at(i));
        QList<PreparedValue> keys;
        keys.reserve(fields.size());
        for (const QString &field : fields)
            keys.append(prepareFieldValue(field, sortKey(tracks.at(i), source, field), options));
        keyed.append(Keyed { i, keys });
    }

    // Each key by T-COMPARE, the weightiest first; tracks alike in every key
    // are in order of path (and, failing that, as they were given).
    std::stable_sort(keyed.begin(), keyed.end(), [&](const Keyed &a, const Keyed &b) {
        for (qsizetype k = 0; k < fields.size(); ++k) {
            const int c = comparePrepared(a.keys.at(k), b.keys.at(k));
            if (c != 0)
                return c < 0;
        }
        return tracks.at(a.at).path < tracks.at(b.at).path;
    });

    QList<LibraryTrack> sorted;
    sorted.reserve(tracks.size());
    for (const Keyed &entry : keyed)
        sorted.append(tracks.at(entry.at));
    return sorted;
}

struct AlbumOrder::Private {
    // What is remembered of one track: the tags it was last read from (shared,
    // so that a track whose tags did not change compares equal at once), the
    // key texts and the path, and the keys prepared from them.
    struct Entry {
        QMap<QString, QStringList> fields;
        QString path;
        QStringList texts;
        QList<PreparedValue> keys;
        quint64 seen = 0; // the call that last gave this track
    };

    SortOptions options;
    QHash<qint64, Entry> kept;
    QList<qint64> order; // the ids, in the order the last call gave back
    quint64 calls = 0;
};

AlbumOrder::AlbumOrder(const SortOptions &options)
    : d(new Private { options, { }, { }, 0 })
{
}

AlbumOrder::~AlbumOrder() = default;

QList<LibraryTrack> AlbumOrder::sorted(const QList<LibraryTrack> &tracks)
{
    const QStringList fields = albumSortFields();
    const qsizetype count = tracks.size();
    const quint64 call = ++d->calls;

    // Per track, as given: its prepared keys, and whether its place among the
    // others may have changed (new to us, a key text or the path is other).
    QList<QList<PreparedValue>> keys;
    keys.reserve(count);
    QList<char> changed(count, 0);
    QHash<qint64, qsizetype> at;
    at.reserve(count);
    for (qsizetype i = 0; i < count; ++i) {
        const LibraryTrack &track = tracks.at(i);
        at.insert(track.id, i);
        auto found = d->kept.find(track.id);
        if (found == d->kept.end()) {
            found = d->kept.insert(track.id, Private::Entry { });
            changed[i] = 1;
        } else if (found->seen == call) {
            changed[i] = 1; // the id is given twice: the later one is looked at afresh
        } else if (found->path == track.path && found->fields == track.tags.fields) {
            // Nothing the keys are made from differs: no text is worked out.
            found->seen = call;
            keys.append(found->keys);
            continue;
        }
        Private::Entry &entry = *found;
        const LibraryFieldSource source(track);
        QStringList texts;
        texts.reserve(fields.size());
        for (const QString &field : fields)
            texts.append(sortKey(track, source, field));
        if (changed[i] || texts != entry.texts || track.path != entry.path) {
            entry.keys.clear();
            entry.keys.reserve(fields.size());
            for (qsizetype k = 0; k < fields.size(); ++k)
                entry.keys.append(prepareFieldValue(fields.at(k), texts.at(k), d->options));
            changed[i] = 1;
        }
        entry.texts = texts;
        entry.path = track.path;
        entry.fields = track.tags.fields;
        entry.seen = call;
        keys.append(entry.keys);
    }
    if (at.size() != count)
        d->order.clear(); // an id given twice: no earlier order to lean on
    // What was not given is forgotten.
    if (d->kept.size() != at.size()) {
        for (auto it = d->kept.begin(); it != d->kept.end();) {
            if (it->seen != call)
                it = d->kept.erase(it);
            else
                ++it;
        }
    }

    // Each key by T-COMPARE, the weightiest first; tracks alike in every key
    // are in order of path and, failing that, as they were given.
    const auto before = [&](qsizetype a, qsizetype b) {
        for (qsizetype k = 0; k < fields.size(); ++k) {
            const int c = comparePrepared(keys.at(a).at(k), keys.at(b).at(k));
            if (c != 0)
                return c < 0;
        }
        const QString &pathA = tracks.at(a).path;
        const QString &pathB = tracks.at(b).path;
        if (pathA != pathB)
            return pathA < pathB;
        return a < b;
    };

    // The tracks that did not change are in order already, from last time:
    // only the changed ones are sorted, and put in among them.
    QList<qsizetype> unchanged;
    QList<qsizetype> moved;
    unchanged.reserve(count);
    for (qsizetype i = 0; i < count; ++i) {
        if (changed.at(i))
            moved.append(i);
    }
    for (const qint64 id : std::as_const(d->order)) {
        const auto found = at.constFind(id);
        if (found != at.cend() && !changed.at(found.value()))
            unchanged.append(found.value());
    }
    QList<qsizetype> result;
    result.reserve(count);
    bool merged = unchanged.size() + moved.size() == count;
    if (merged) {
        std::sort(moved.begin(), moved.end(), before);
        std::merge(unchanged.cbegin(), unchanged.cend(), moved.cbegin(), moved.cend(),
            std::back_inserter(result), before);
        // The order from last time is only trusted if it still holds (a tie
        // between tracks given in another order is not one it can know of).
        for (qsizetype i = 1; i < result.size() && merged; ++i)
            merged = !before(result.at(i), result.at(i - 1));
    }
    if (!merged) {
        result.clear();
        for (qsizetype i = 0; i < count; ++i)
            result.append(i);
        std::sort(result.begin(), result.end(), before);
    }

    QList<LibraryTrack> sorted;
    sorted.reserve(count);
    d->order.clear();
    d->order.reserve(count);
    for (const qsizetype i : std::as_const(result)) {
        sorted.append(tracks.at(i));
        d->order.append(tracks.at(i).id);
    }
    if (at.size() != count)
        d->order.clear();
    return sorted;
}

QList<AlbumGroup> albumGroups(const QList<LibraryTrack> &tracksInOrder)
{
    QList<AlbumGroup> groups;
    QString previousDisc; // the Disc# of the track before the one in hand
    for (const LibraryTrack &track : tracksInOrder) {
        const QString artist = browseArtist(track);
        const QString album = firstValue(track, QStringLiteral("Album"));
        const QString disc = firstValue(track, QStringLiteral("Disc#"));

        // The run of the track before it, when artist and album are the same.
        if (groups.isEmpty() || !sameArtist(groups.last().artist, artist)
            || !sameAlbum(groups.last().album, album)) {
            AlbumGroup group;
            group.artist = artist;
            group.album = album;
            groups.append(group);
        }
        AlbumGroup &group = groups.last();

        if (group.year.isEmpty())
            group.year = browseValues(track, QStringLiteral("Year")).value(0);
        if (group.genre.isEmpty())
            group.genre = firstValue(track, QStringLiteral("Genre"));
        if (!group.trackIds.isEmpty() && disc != previousDisc)
            group.discStarts.append(int(group.trackIds.size()));
        previousDisc = disc;

        group.totalMs += track.tags.audio.durationMs;
        group.trackIds.append(track.id);
        group.paths.append(track.path);
    }
    return groups;
}

QString albumHeading(const AlbumGroup &group)
{
    return group.album.isEmpty() ? QStringLiteral("[Unknown Album]") : group.album;
}

QString albumDetail(const AlbumGroup &group)
{
    const qint64 count = group.trackIds.size();
    // Whole minutes, to the nearest; half a minute goes up.
    const qint64 minutes = (std::max<qint64>(0, group.totalMs) + 30000) / 60000;

    QStringList parts;
    parts.append(group.artist.isEmpty() ? QStringLiteral("[Unknown Artist]") : group.artist);
    if (!group.year.isEmpty())
        parts.append(group.year);
    if (!group.genre.isEmpty())
        parts.append(group.genre);
    parts.append(count == 1 ? QStringLiteral("1 track") : QStringLiteral("%1 tracks").arg(count));
    parts.append(QStringLiteral("%1 min").arg(minutes));
    return parts.join(QStringLiteral(" \u2022 "));
}

int jumpIndex(const QList<AlbumGroup> &groups, QChar letter, const SortOptions &options)
{
    const QChar want = letter.toUpper();
    const bool wantsLetter = isLatinCapital(want);
    if (!wantsLetter && want != QLatin1Char('#'))
        return -1;
    for (qsizetype i = 0; i < groups.size(); ++i) {
        const QChar first = jumpCharacter(groups.at(i).artist, options);
        if (wantsLetter ? first == want : !isLatinCapital(first))
            return int(i);
    }
    return -1;
}

}
