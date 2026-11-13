#include "autoplaylist.h"

#include <algorithm>
#include <cstdint>
#include <random>

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

    orderCandidates(&candidates, playlist.selectedBy, seed);

    QList<qint64> ids;
    ids.reserve(candidates.size());
    for (const LibraryTrack *track : candidates)
        ids.append(track->id);
    return ids;
}

}
