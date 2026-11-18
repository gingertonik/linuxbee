#include "itunesxml.h"

#include "plist.h"

#include <QtGlobal>

#include <limits>

// I-PARSE and I-PLAN. Contract: docs/spec/import.md.
namespace linuxbee {

namespace {

using plist::Value;

// The integer under a key, or nothing when the key is absent or not an integer.
std::optional<qint64> integerOf(const Value &dict, const QString &key)
{
    const Value *v = dict.find(key);
    if (!v || v->type != Value::Type::Integer)
        return std::nullopt;
    return v->integer;
}

// A track or playlist number that can be an ImportedTrack::trackId.
std::optional<int> idOf(const Value &dict, const QString &key)
{
    const auto id = integerOf(dict, key);
    if (!id || *id <= 0 || *id > std::numeric_limits<int>::max())
        return std::nullopt;
    return static_cast<int>(*id);
}

QString stringOf(const Value &dict, const QString &key)
{
    const Value *v = dict.find(key);
    return v && v->type == Value::Type::String ? v->text : QString();
}

bool isTrue(const Value &dict, const QString &key)
{
    const Value *v = dict.find(key);
    return v && v->type == Value::Type::Bool && v->boolean;
}

// A play or skip count: a negative number is 0.
int countOf(const Value &dict, const QString &key)
{
    const auto n = integerOf(dict, key);
    if (!n || *n < 0)
        return 0;
    return static_cast<int>(qMin<qint64>(*n, std::numeric_limits<int>::max()));
}

QDateTime dateOf(const Value &dict, const QString &key)
{
    const Value *v = dict.find(key);
    return v && v->type == Value::Type::Date ? v->date : QDateTime();
}

// Rating 0 to 100 becomes stars: the number divided by 10, rounded (a half
// goes up), divided by 2. Below 10 there is no rating. A number above 100
// is read as 100, which the spec does not cover.
std::optional<double> ratingOf(const Value &track)
{
    if (isTrue(track, QStringLiteral("Rating Computed")))
        return std::nullopt;
    const auto raw = integerOf(track, QStringLiteral("Rating"));
    if (!raw || *raw < 10)
        return std::nullopt;
    const qint64 capped = qMin<qint64>(*raw, 100);
    const qint64 tenths = (capped + 5) / 10; // round half up, integer arithmetic
    return static_cast<double>(tenths) / 2.0;
}

}

ImportedLibrary parseItunesLibrary(const QByteArray &xml)
{
    ImportedLibrary lib;
    Value root;
    if (!plist::parse(xml, &root, &lib.error))
        return lib;
    if (root.type != Value::Type::Dict) {
        lib.error = QStringLiteral("the property list is not a dict");
        return lib;
    }
    const Value *tracks = root.find(QStringLiteral("Tracks"));
    if (!tracks || tracks->type != Value::Type::Dict) {
        lib.error = QStringLiteral("the library has no Tracks dict");
        return lib;
    }

    for (const auto &entry : tracks->dict) {
        const Value &dict = entry.second;
        if (dict.type != Value::Type::Dict)
            continue;
        const auto id = idOf(dict, QStringLiteral("Track ID"));
        if (!id)
            continue;
        const QString location = pathFromItunesLocation(stringOf(dict, QStringLiteral("Location")));
        if (location.isEmpty()) // no location, or a stream
            continue;

        ImportedTrack track;
        track.trackId = *id;
        track.location = location;
        track.name = stringOf(dict, QStringLiteral("Name"));
        track.artist = stringOf(dict, QStringLiteral("Artist"));
        track.album = stringOf(dict, QStringLiteral("Album"));
        track.rating = ratingOf(dict);
        track.love = stringOf(dict, QStringLiteral("Love")).trimmed();
        track.playCount = countOf(dict, QStringLiteral("Play Count"));
        track.skipCount = countOf(dict, QStringLiteral("Skip Count"));
        track.dateAdded = dateOf(dict, QStringLiteral("Date Added"));
        track.lastPlayed = dateOf(dict, QStringLiteral("Play Date UTC"));
        lib.tracks.append(track);
    }

    // The master list, built-in and smart lists, folders and unnamed lists are left out.
    const Value *lists = root.find(QStringLiteral("Playlists"));
    if (lists && lists->type == Value::Type::Array) {
        for (const Value &dict : lists->array) {
            if (dict.type != Value::Type::Dict)
                continue;
            if (isTrue(dict, QStringLiteral("Master")) || isTrue(dict, QStringLiteral("Folder"))
                || dict.find(QStringLiteral("Distinguished Kind"))
                || dict.find(QStringLiteral("Smart Info")))
                continue;
            const QString name = stringOf(dict, QStringLiteral("Name")).trimmed();
            if (name.isEmpty())
                continue;

            ImportedPlaylist playlist;
            playlist.name = name;
            const Value *items = dict.find(QStringLiteral("Playlist Items"));
            if (items && items->type == Value::Type::Array) {
                for (const Value &item : items->array) {
                    if (item.type != Value::Type::Dict)
                        continue;
                    if (const auto id = idOf(item, QStringLiteral("Track ID")))
                        playlist.trackIds.append(*id);
                }
            }
            lib.playlists.append(playlist);
        }
    }

    lib.ok = true;
    return lib;
}

}
