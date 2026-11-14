#include "autoplaylist.h"

#include <algorithm>
#include <cstdint>
#include <random>
#include <set>
#include <utility>

#include "search/search.h"

namespace linuxbee {

namespace {

// A-SELECT: the rating as a number; no rating, or one that is not a number, counts as 0.
double ratingOf(const LibraryTrack &track)
{
    const QStringList values = track.tags.fields.value(QStringLiteral("Rating"));
    if (values.isEmpty())
        return 0;
    bool ok = false;
    const double rating = values.first().toDouble(&ok);
    return ok ? rating : 0;
}

// A-SELECT: a track never played comes last under the most-recently order and
// first under the least-recently order.
bool playedBefore(const LibraryTrack &a, const LibraryTrack &b, bool mostRecentFirst)
{
    const bool aPlayed = a.lastPlayed.isValid();
    const bool bPlayed = b.lastPlayed.isValid();
    if (aPlayed != bPlayed)
        return mostRecentFirst ? aPlayed : !aPlayed;
    if (!aPlayed)
        return false;
    return mostRecentFirst ? a.lastPlayed > b.lastPlayed : a.lastPlayed < b.lastPlayed;
}

// Put the candidates in the order the playlist asks for. Ties keep their
// earlier order, which stable_sort guarantees.
void orderCandidates(
    QList<const LibraryTrack *> *candidates, AutoPlaylist::SelectedBy selectedBy, quint32 seed)
{
    using S = AutoPlaylist::SelectedBy;
    switch (selectedBy) {
    case S::Random: {
        // Fisher-Yates over mt19937, whose output the standard fixes, so one seed
        // gives one order on every machine.
        std::mt19937 engine(seed);
        for (qsizetype i = candidates->size() - 1; i > 0; --i) {
            const qsizetype j = qsizetype(engine() % std::uint32_t(i + 1));
            std::swap((*candidates)[i], (*candidates)[j]);
        }
        return;
    }
    case S::HighestRating:
        std::stable_sort(candidates->begin(), candidates->end(),
            [](const LibraryTrack *a, const LibraryTrack *b) {
                return ratingOf(*a) > ratingOf(*b);
            });
        return;
    case S::LowestRating:
        std::stable_sort(candidates->begin(), candidates->end(),
            [](const LibraryTrack *a, const LibraryTrack *b) {
                return ratingOf(*a) < ratingOf(*b);
            });
        return;
    case S::MostRecentlyPlayed:
        std::stable_sort(candidates->begin(), candidates->end(),
            [](const LibraryTrack *a, const LibraryTrack *b) {
                return playedBefore(*a, *b, true);
            });
        return;
    case S::LeastRecentlyPlayed:
        std::stable_sort(candidates->begin(), candidates->end(),
            [](const LibraryTrack *a, const LibraryTrack *b) {
                return playedBefore(*a, *b, false);
            });
        return;
    case S::MostOftenPlayed:
        std::stable_sort(candidates->begin(), candidates->end(),
            [](const LibraryTrack *a, const LibraryTrack *b) {
                return a->playCount > b->playCount;
            });
        return;
    case S::LeastOftenPlayed:
        std::stable_sort(candidates->begin(), candidates->end(),
            [](const LibraryTrack *a, const LibraryTrack *b) {
                return a->playCount < b->playCount;
            });
        return;
    case S::MostRecentlyAdded:
        std::stable_sort(candidates->begin(), candidates->end(),
            [](const LibraryTrack *a, const LibraryTrack *b) {
                return a->dateAdded > b->dateAdded;
            });
        return;
    case S::LeastRecentlyAdded:
        std::stable_sort(candidates->begin(), candidates->end(),
            [](const LibraryTrack *a, const LibraryTrack *b) {
                return a->dateAdded < b->dateAdded;
            });
        return;
    }
}

// A-DUP and A-LIMIT compare text the way S-FOLD does, trimmed at both ends.
QString foldedText(const QString &text)
{
    return foldForSearch(text).trimmed();
}

QString firstValue(const LibraryTrack &track, const QString &field)
{
    const QStringList values = track.tags.fields.value(field);
    return values.isEmpty() ? QString() : values.first();
}

// A-DUP: the artist values joined by "; ", and the first title, both folded.
std::pair<QString, QString> duplicateKey(const LibraryTrack &track)
{
    const QStringList artists = track.tags.fields.value(QStringLiteral("Artist"));
    return { foldedText(artists.join(QStringLiteral("; "))),
        foldedText(firstValue(track, QStringLiteral("Title"))) };
}

// A-DUP: the first candidate in the order the tracks were given is kept.
void removeDuplicates(QList<const LibraryTrack *> *candidates)
{
    std::set<std::pair<QString, QString>> seen;
    QList<const LibraryTrack *> kept;
    for (const LibraryTrack *track : *candidates) {
        if (seen.insert(duplicateKey(*track)).second)
            kept.append(track);
    }
    *candidates = kept;
}

// A-LIMIT: tracks are taken from the front while the total stays at or under
// the limit. The first track that would go over ends the list.
template <typename Weigh>
void takeWithin(QList<const LibraryTrack *> *ordered, qint64 limit, Weigh weigh)
{
    qint64 total = 0;
    qsizetype count = 0;
    for (; count < ordered->size(); ++count) {
        total += weigh(*(*ordered)[count]);
        if (total > limit)
            break;
    }
    ordered->resize(count);
}

// A-LIMIT: an album is its Album Artist and its Album, first values, folded.
std::pair<QString, QString> albumKey(const LibraryTrack &track)
{
    return { foldedText(firstValue(track, QStringLiteral("Album Artist"))),
        foldedText(firstValue(track, QStringLiteral("Album"))) };
}

// A-LIMIT: cut the ordered candidates to what the limit allows.
void applyLimit(QList<const LibraryTrack *> *ordered, const AutoPlaylist &playlist)
{
    using U = AutoPlaylist::LimitUnit;
    if (!playlist.limitEnabled)
        return;
    const qint64 amount = playlist.limitAmount;
    if (amount < 1) {
        ordered->clear();
        return;
    }
    switch (playlist.limitUnit) {
    case U::Items:
        if (ordered->size() > amount)
            ordered->resize(qsizetype(amount));
        return;
    case U::Minutes:
        takeWithin(ordered, amount * 60000,
            [](const LibraryTrack &t) { return qint64(t.tags.audio.durationMs); });
        return;
    case U::Hours:
        takeWithin(ordered, amount * 3600000,
            [](const LibraryTrack &t) { return qint64(t.tags.audio.durationMs); });
        return;
    case U::Megabytes:
        takeWithin(ordered, amount * 1048576, [](const LibraryTrack &t) { return t.sizeBytes; });
        return;
    case U::Gigabytes:
        takeWithin(ordered, amount * 1073741824, [](const LibraryTrack &t) { return t.sizeBytes; });
        return;
    case U::Albums: {
        // The first `amount` different albums met are chosen; every track of a
        // chosen album stays, wherever it stands in the order.
        std::set<std::pair<QString, QString>> chosen;
        for (const LibraryTrack *track : *ordered) {
            const auto key = albumKey(*track);
            if (!chosen.count(key) && qint64(chosen.size()) < amount)
                chosen.insert(key);
        }
        QList<const LibraryTrack *> kept;
        for (const LibraryTrack *track : *ordered) {
            if (chosen.count(albumKey(*track)))
                kept.append(track);
        }
        *ordered = kept;
        return;
    }
    }
}

}

QList<qint64> evaluateAutoPlaylist(const AutoPlaylist &playlist, const QList<LibraryTrack> &tracks,
    const RuleContext &context, quint32 seed)
{
    // A-MATCH: a missing track never matches; the rules decide the rest. An
    // invalid rule set matches nothing (U-MATCH).
    QList<const LibraryTrack *> candidates;
    for (const LibraryTrack &track : tracks) {
        if (track.missing)
            continue;
        if (matchesRules(playlist.rules, LibraryFieldSource(track), context))
            candidates.append(&track);
    }

    // A-DUP runs before the order, so the kept track is the first one given.
    if (playlist.filterDuplicates)
        removeDuplicates(&candidates);

    orderCandidates(&candidates, playlist.selectedBy, seed);
    applyLimit(&candidates, playlist);

    QList<qint64> ids;
    ids.reserve(candidates.size());
    for (const LibraryTrack *track : candidates)
        ids.append(track->id);
    return ids;
}

}
