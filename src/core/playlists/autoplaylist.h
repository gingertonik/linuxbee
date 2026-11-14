#pragma once
#include <QList>
#include <QString>

#include "library/library.h"
#include "rules/rules.h"

// Auto-playlists: a playlist that is a question, answered again each time.
// Contract: docs/spec/auto-playlists.md. Pure: tracks in, ids out.
namespace linuxbee {

struct AutoPlaylist {
    RuleSet rules;

    enum class LimitUnit { Items, Minutes, Hours, Megabytes, Gigabytes, Albums };
    bool limitEnabled = false;
    int limitAmount = 25;
    LimitUnit limitUnit = LimitUnit::Items;

    enum class SelectedBy {
        Random,
        HighestRating,
        LowestRating,
        MostRecentlyPlayed,
        LeastRecentlyPlayed,
        MostOftenPlayed,
        LeastOftenPlayed,
        MostRecentlyAdded,
        LeastRecentlyAdded,
    };
    SelectedBy selectedBy = SelectedBy::Random;

    bool filterDuplicates = false; // one track per artist and title
};

// A-NAMES: "items", "minutes", ... and "random", "highest-rating", ...
QString limitUnitId(AutoPlaylist::LimitUnit unit);
QString selectedById(AutoPlaylist::SelectedBy selectedBy);

// A-JSON
QString autoPlaylistToJson(const AutoPlaylist &playlist);
bool autoPlaylistFromJson(const QString &json, AutoPlaylist *playlist, QString *error);

// A-EVALUATE: the ids of the tracks the playlist holds, in order. `seed`
// drives every random choice: the same seed gives the same answer.
QList<qint64> evaluateAutoPlaylist(const AutoPlaylist &playlist, const QList<LibraryTrack> &tracks,
    const RuleContext &context, quint32 seed);

}
