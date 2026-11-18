#include "library.h"

#include <algorithm>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QTimeZone>

#include "library_p.h"
#include "tags/tagreader.h"

namespace linuxbee {
namespace {

// L-STORE: the library keeps how many pictures a file has and none of their
// bytes, so a read asks for the pictures without their data.
const ReadOptions kReadForLibrary { true, false };

// L-EDIT refresh: one file read the way the library stores it, which is what
// readTags returned, the count of its pictures, and the file's own size and
// modification time with the time in UTC to the second. False, having changed
// nothing, when the file is not readable as audio.
bool readFresh(const QString &path, LibraryTrack *track)
{
    const ReadResult result = readTags(path, kReadForLibrary);
    if (result.status != ReadResult::Status::Ok)
        return false;
    const QFileInfo info(path);
    const QDateTime stamp = info.fileTime(QFileDevice::FileModificationTime);
    track->path = path;
    track->tags = result.tags;
    track->pictureCount = int(result.tags.pictures.size());
    track->tags.pictures.clear(); // L-STORE: the bytes are never stored
    track->sizeBytes = info.size();
    track->modified = stamp.isValid()
        ? QDateTime::fromSecsSinceEpoch(stamp.toSecsSinceEpoch(), QTimeZone::UTC)
        : QDateTime();
    return true;
}

// L-MOVED: one track to a new path, with the refusals of setPath. False for a
// refusal. `touched` says the stored row changed, and `was` is set to the path
// the track held when its path did. `refused` is set to the database's words only when the store
// itself refused the change (L-FAILED), and stays empty for every other false.
bool moveTrack(library::Store &store, qint64 id, const QString &newPath, QString *was,
    bool *touched, QString *refused)
{
    *touched = false;
    const std::optional<LibraryTrack> known = store.byId(id);
    if (!known)
        return false;
    const QString clean = libraryPath(newPath);
    const std::optional<LibraryTrack> other = store.byPath(clean);
    if (other && other->id != id)
        return false; // L-EDIT: another track has that path
    // L-MISSING: missing is set by whether a file is at the new path.
    const bool missing = !QFileInfo::exists(clean);
    // L-SIGNALS: a call that changes nothing emits nothing.
    if (known->path == clean && known->missing == missing)
        return true;
    if (!store.setPath(id, clean, missing)) {
        *refused = store.lastError();
        return false;
    }
    *touched = true;
    if (known->path != clean)
        *was = known->path;
    return true;
}

// L-FAILED: nothing to do is found out before the lock is asked for, so a call asks the store with
// a plain read first. True when some id in `ids` is a track of ours that `wants` accepts. It stops
// at the first one, so a call that has something to do reads little. The answer is taken outside
// the lock and can be stale by the time it is acted on; that costs a call that comes a moment late.
template <typename Wants>
bool anyWanted(const library::Store &store, const QList<qint64> &ids, Wants wants)
{
    return std::any_of(ids.cbegin(), ids.cend(), [&store, &wants](qint64 id) {
        const std::optional<LibraryTrack> known = store.byId(id);
        return known && wants(*known);
    });
}

} // namespace

Library::Library()
    : d(std::make_unique<Private>())
{
}

Library::~Library() = default;

// L-OPEN: opens the file, creating it when it does not exist, and reports the
// reason when the file cannot be used.
std::unique_ptr<Library> Library::open(const QString &databasePath, QString *error)
{
    // The constructor is private, so the unique_ptr is the owner from here.
    std::unique_ptr<Library> library(new Library());
    QString reason;
    if (!library->d->store.open(databasePath, &reason)) {
        if (error)
            *error = reason;
        return nullptr;
    }
    return library;
}

// L-CATEGORY (W-L): the nodes a MusicBee library has, in that order.
QStringList Library::categories()
{
    return { QStringLiteral("Music"), QStringLiteral("Inbox"), QStringLiteral("Audiobooks"),
        QStringLiteral("Podcasts"), QStringLiteral("Videos") };
}

// L-SCHEMA: the number this version writes, and the highest it accepts.
int Library::currentSchemaVersion()
{
    return library::Store::schemaVersion();
}

// L-SCAN and L-ADD live in scan.cpp.

int Library::count() const
{
    return d->store.count();
}

// L-COUNT: one counting query, every section present, 0 for an empty one.
// Reads no track, so it counts nothing in tracksRead().
QMap<QString, int> Library::categoryCounts() const
{
    QMap<QString, int> counts = d->store.categoryCounts();
    for (const QString &name : categories()) {
        if (!counts.contains(name))
            counts.insert(name, 0);
    }
    return counts;
}

// L-COUNT: every track this object has handed out since it was opened.
qint64 Library::tracksRead() const
{
    return d->tracksHandedOut;
}

QList<LibraryTrack> Library::tracks(const QString &category) const
{
    QList<LibraryTrack> list = d->store.all(category);
    d->tracksHandedOut += static_cast<qint64>(list.size());
    return list;
}

std::optional<LibraryTrack> Library::track(qint64 id) const
{
    std::optional<LibraryTrack> found = d->store.byId(id);
    if (found)
        ++d->tracksHandedOut;
    return found;
}

// L-STORE: paths are stored clean, so a lookup is cleaned the same way.
std::optional<LibraryTrack> Library::trackByPath(const QString &path) const
{
    std::optional<LibraryTrack> found = d->store.byPath(libraryPath(path));
    if (found)
        ++d->tracksHandedOut;
    return found;
}

// L-EDIT remove: forgets the tracks. No music file is touched.
int Library::remove(const QList<qint64> &ids)
{
    // L-FAILED: no id is a track of ours, so there is nothing to refuse.
    if (!anyWanted(d->store, ids, [](const LibraryTrack &) { return true; }))
        return 0;
    const QList<qint64> gone = d->store.removeIds(ids);
    // L-FAILED: a refusal changed nothing, so there is no change to announce.
    if (const QString why = d->store.lastError(); !why.isEmpty()) {
        emit failed(why);
        return 0;
    }
    // L-SIGNALS: one signal for the call, carrying the ids it changed.
    if (!gone.isEmpty())
        emit tracksRemoved(gone);
    return static_cast<int>(gone.size());
}

// L-EDIT setCategory: counts the tracks it changed, so an id that is unknown,
// or already in that category, does not count.
int Library::setCategory(const QList<qint64> &ids, const QString &category)
{
    // L-CATEGORY: a function given any other name does nothing.
    if (!categories().contains(category))
        return 0;
    // L-FAILED: no id is ours, or every one named is in that section already.
    if (!anyWanted(d->store, ids,
            [&category](const LibraryTrack &track) { return track.category != category; }))
        return 0;
    const QList<qint64> changed = d->store.setCategory(ids, category);
    if (const QString why = d->store.lastError(); !why.isEmpty()) {
        emit failed(why); // L-FAILED
        return 0;
    }
    if (!changed.isEmpty())
        emit tracksUpdated(changed);
    return static_cast<int>(changed.size());
}

// L-EDIT refresh: the file is read again now, whatever its time says.
bool Library::refresh(qint64 id)
{
    const std::optional<LibraryTrack> known = d->store.byId(id);
    if (!known)
        return false;
    LibraryTrack fresh;
    if (!readFresh(known->path, &fresh))
        return false; // L-EDIT: no change when the file cannot be read
    if (!d->store.updateReading(id, fresh)) {
        if (const QString why = d->store.lastError(); !why.isEmpty())
            emit failed(why); // L-FAILED
        return false;
    }
    emit tracksUpdated(QList<qint64> { id });
    return true;
}

// L-EDIT setPath: records that the file moved. The track keeps its id and
// everything the library knows about it.
bool Library::setPath(qint64 id, const QString &newPath)
{
    QString was;
    QString refused;
    bool touched = false;
    if (!moveTrack(d->store, id, newPath, &was, &touched, &refused)) {
        if (!refused.isEmpty())
            emit failed(refused); // L-FAILED
        return false;
    }
    if (!touched)
        return true;
    emit tracksUpdated(QList<qint64> { id });
    // L-MOVED: after the change is stored and after tracksUpdated.
    if (!was.isEmpty())
        emit pathsMoved(QHash<QString, QString> { { was, libraryPath(newPath) } });
    return true;
}

// L-EDIT relinkPaths: a folder was moved or mounted elsewhere, so every path
// under it is rewritten. The prefixes are folders: a trailing slash is ignored,
// and ".../a" is not a prefix of ".../ab/x.mp3".
int Library::relinkPaths(const QString &oldPrefix, const QString &newPrefix)
{
    const QString from = libraryPath(oldPrefix);
    const QString to = libraryPath(newPrefix);
    if (from.isEmpty() || to.isEmpty())
        return 0;
    const QString under = from + QLatin1Char('/');
    // L-FAILED: with nothing under the folder there is nothing to change, and no lock is taken.
    const auto holdsSome = [&under](const QList<LibraryTrack> &all) {
        return std::any_of(all.cbegin(), all.cend(),
            [&under](const LibraryTrack &track) { return track.path.startsWith(under); });
    };
    if (!holdsSome(d->store.all(QString())))
        return 0;
    // The change is one, as recordMoves is: the write lock first (L-WAIT), and the first step the
    // database refuses gives the whole of it up, says so once and stores nothing.
    const auto refuse = [this]() {
        const QString why = d->store.lastError();
        d->store.rollbackBatch();
        emit failed(why.isEmpty() ? QStringLiteral("the database refused the change") : why);
        return 0;
    };
    if (!d->store.beginBatch())
        return refuse();
    QList<qint64> changed;
    QHash<QString, QString> moves;
    // Looked at again with the lock held, so no other program changes a track between the look
    // and the storing.
    for (const LibraryTrack &track : d->store.all(QString())) {
        if (!track.path.startsWith(under))
            continue;
        const QString moved = to + QLatin1Char('/') + track.path.mid(from.size() + 1);
        if (d->store.byPath(moved))
            continue; // L-EDIT: a track whose new path is another's is left alone
        // L-MISSING: missing is set by whether a file is at the new path.
        if (!d->store.setPath(track.id, moved, !QFileInfo::exists(moved)))
            return refuse();
        changed.append(track.id);
        moves.insert(track.path, moved);
    }
    if (!d->store.commitBatch())
        return refuse();
    if (!changed.isEmpty())
        emit tracksUpdated(changed);
    // L-MOVED: one signal for the call, with every path it changed.
    if (!moves.isEmpty())
        emit pathsMoved(moves);
    return static_cast<int>(changed.size());
}

namespace {

// L-WAIT: a change nobody is waiting for waits the quick wait, and the connection goes back to
// the write wait however the call ends, so no other change is affected.
struct QuickWaitScope {
    library::Store &store;
    std::chrono::milliseconds back;
    QuickWaitScope(
        library::Store &store, std::chrono::milliseconds quick, std::chrono::milliseconds back)
        : store(store)
        , back(back)
    {
        store.setWriteWait(quick);
    }
    ~QuickWaitScope() { store.setWriteWait(back); }
    QuickWaitScope(const QuickWaitScope &) = delete;
    QuickWaitScope &operator=(const QuickWaitScope &) = delete;
};

}

// L-STATS recordPlay: the count goes up, and "last played" moves forward only.
bool Library::recordPlay(qint64 id, const QDateTime &when)
{
    // L-STATS: an unknown id or an invalid time changes nothing.
    if (!when.isValid())
        return false;
    bool known = false;
    bool stored = false;
    QString lookWords;
    {
        // L-WAIT: the look-up waits the short wait too.
        const QuickWaitScope quick(d->store, d->quickWait, d->writeWait);
        known = d->store.byId(id).has_value();
        lookWords = d->store.lastReadError();
        if (known)
            stored = d->store.recordPlay(id, when);
    } // back to the write wait before any signal goes out
    if (!known) {
        // L-FAILED: an unknown id is nothing to do, whoever holds the library; a look that could
        // not be made is not "no such track".
        if (!lookWords.isEmpty())
            emit failed(lookWords);
        return false;
    }
    if (!stored) {
        if (const QString why = d->store.lastError(); !why.isEmpty())
            emit failed(why); // L-FAILED: a play that cannot be counted is said
        return false;
    }
    emit tracksUpdated(QList<qint64> { id });
    return true;
}

// L-STATS recordSkip: counts a skip and changes nothing else, so `when` is
// checked, because an invalid time changes nothing, and never stored.
bool Library::recordSkip(qint64 id, const QDateTime &when)
{
    if (!when.isValid())
        return false;
    bool known = false;
    bool stored = false;
    QString lookWords;
    {
        const QuickWaitScope quick(d->store, d->quickWait, d->writeWait);
        known = d->store.byId(id).has_value();
        lookWords = d->store.lastReadError();
        if (known)
            stored = d->store.recordSkip(id);
    } // back to the write wait before any signal goes out
    if (!known) {
        if (!lookWords.isEmpty())
            emit failed(lookWords); // L-FAILED: as recordPlay
        return false;
    }
    if (!stored) {
        if (const QString why = d->store.lastError(); !why.isEmpty())
            emit failed(why); // L-FAILED
        return false;
    }
    emit tracksUpdated(QList<qint64> { id });
    return true;
}

// L-STATS recordPlays: the plays and skips of files, in the order they happened, stored in one
// change under the short wait. Each different path is looked up once; a record whose file the
// library does not have, or whose time is not valid, is passed over without asking for the lock.
// What is returned is what the library refused, in the order given.
QList<PlayRecord> Library::recordPlays(const QList<PlayRecord> &records)
{
    struct Counted {
        qint64 id = 0;
        qsizetype at = 0; // the position in `records`
    };
    QList<Counted> counted;
    QString refusedWords;
    bool refused = false;
    bool lookFailed = false; // a look-up could not be made, so nothing is known of the rest
    {
        const QuickWaitScope quick(d->store, d->quickWait, d->writeWait);
        QHash<QString, qint64> ids; // by clean path; 0: the library does not have it
        for (qsizetype i = 0; i < records.size(); ++i) {
            const PlayRecord &record = records.at(i);
            if (!record.when.isValid())
                continue;
            const QString path = libraryPath(record.path);
            auto found = ids.find(path);
            if (found == ids.end()) {
                const std::optional<LibraryTrack> track = d->store.byPath(path);
                if (!track && !d->store.lastReadError().isEmpty()) {
                    refusedWords = d->store.lastReadError(); // the look could not be made
                    refused = true;
                    lookFailed = true;
                    break;
                }
                found = ids.insert(path, track ? track->id : 0);
            }
            if (found.value() != 0)
                counted.append({ found.value(), i });
        }
        if (!refused && !counted.isEmpty()) {
            // The rest in one change: all or none. The first refused step gives it up.
            const auto refuse = [this, &refusedWords, &refused]() {
                refusedWords = d->store.lastError();
                d->store.rollbackBatch();
                refused = true;
            };
            if (!d->store.beginBatch()) {
                refuse();
            } else {
                for (const Counted &one : std::as_const(counted)) {
                    const PlayRecord &record = records.at(one.at);
                    const bool done = record.skip ? d->store.recordSkip(one.id)
                                                  : d->store.recordPlay(one.id, record.when);
                    // False with no words is a track that went in the meantime: nothing to do.
                    if (!done && !d->store.lastError().isEmpty()) {
                        refuse();
                        break;
                    }
                }
                if (!refused && !d->store.commitBatch())
                    refuse();
            }
        }
    } // back to the write wait before any signal goes out
    if (refused) {
        emit failed(refusedWords.isEmpty() ? QStringLiteral("the database refused the change")
                                           : refusedWords);
        QList<PlayRecord> back;
        if (lookFailed) {
            // Every record with a valid time: what the library has is not known.
            for (const PlayRecord &record : records)
                if (record.when.isValid())
                    back.append(record);
        } else {
            for (const Counted &one : std::as_const(counted))
                back.append(records.at(one.at));
        }
        return back;
    }
    if (counted.isEmpty())
        return { };
    QList<qint64> changed;
    QSet<qint64> seen;
    for (const Counted &one : std::as_const(counted)) {
        if (!seen.contains(one.id)) {
            seen.insert(one.id);
            changed.append(one.id);
        }
    }
    emit tracksUpdated(changed);
    return { };
}

// L-STATS setStatistics: the library's own four facts in one go, for an import.
bool Library::setStatistics(qint64 id, const TrackStatistics &statistics)
{
    // L-STATS: refused, changing nothing, for a missing date added or a
    // negative count. An unknown id is what the store's no-row answer is.
    if (!statistics.dateAdded.isValid() || statistics.playCount < 0 || statistics.skipCount < 0)
        return false;
    if (!d->store.byId(id)) // L-FAILED: as recordPlay
        return false;
    if (!d->store.setStatistics(id, statistics)) {
        if (const QString why = d->store.lastError(); !why.isEmpty())
            emit failed(why); // L-FAILED
        return false;
    }
    emit tracksUpdated(QList<qint64> { id });
    return true;
}

// L-STATS mergeStatistics: an import stored in one change under the write lock, which waits the
// long wait (someone asked). Each track is merged by the database from what it holds now.
int Library::mergeStatistics(const QList<StatisticsMerge> &merges)
{
    if (merges.isEmpty())
        return 0; // nothing asked of the library
    // L-FAILED: the first step the database refuses gives the whole change up and says so once.
    const auto refuse = [this]() {
        const QString why = d->store.lastError();
        d->store.rollbackBatch();
        emit failed(why.isEmpty() ? QStringLiteral("the database refused the change") : why);
        return -1;
    };
    if (!d->store.beginBatch())
        return refuse();
    QList<qint64> changed;
    QSet<qint64> seen;
    for (const StatisticsMerge &merge : merges) {
        if (d->store.mergeStatistics(merge.id, merge.statistics)) {
            if (!seen.contains(merge.id)) {
                seen.insert(merge.id);
                changed.append(merge.id);
            }
        } else if (!d->store.lastError().isEmpty()) {
            return refuse();
        }
    }
    if (!d->store.commitBatch())
        return refuse();
    if (!changed.isEmpty())
        emit tracksUpdated(changed);
    return static_cast<int>(changed.size());
}

// L-MOVED recordMoves: the moves of the files, in the order they happened, done to the tracks
// one after the other, in one transaction. At the end each moved track is missing or not by
// its file, and one signal of each kind goes out. The run is whole or not at all: the first
// step the database refuses gives the whole batch up, runs no statement after it, and says so
// once (L-FAILED).
int Library::recordMoves(const QList<FileMove> &moves)
{
    struct Start {
        QString path;
        bool missing = false;
    };
    // L-FAILED: a call with nothing to do asks for no lock. A move has something to do when its two
    // paths differ and one of them is a track of ours (the track to move, or the one replaced). The
    // library has not changed until the first such move, so looking at the start is enough.
    const bool somethingToDo
        = std::any_of(moves.cbegin(), moves.cend(), [this](const FileMove &move) {
              const QString from = libraryPath(move.from);
              const QString to = libraryPath(move.to);
              return !from.isEmpty() && !to.isEmpty() && from != to
                  && (d->store.byPath(from) || d->store.byPath(to));
          });
    if (!somethingToDo)
        return 0;
    QHash<qint64, Start> began; // each track that moved: how it was before the call
    QList<qint64> order; // the moved tracks, in the order they first moved
    QList<qint64> gone;
    QSet<qint64> goneSet;
    // A refused step ends the call: the batch is given up, nothing is stored or signalled.
    const auto refuse = [this]() {
        const QString why = d->store.lastError();
        d->store.rollbackBatch();
        emit failed(why.isEmpty() ? QStringLiteral("the database refused the change") : why);
        return 0;
    };
    if (!d->store.beginBatch())
        return refuse();
    for (const FileMove &move : moves) {
        const QString from = libraryPath(move.from);
        const QString to = libraryPath(move.to);
        if (from.isEmpty() || to.isEmpty() || from == to)
            continue;
        const std::optional<LibraryTrack> moving = d->store.byPath(from);
        if (const std::optional<LibraryTrack> held = d->store.byPath(to);
            held && (!moving || held->id != moving->id)) {
            d->store.removeIds({ held->id });
            if (!d->store.lastError().isEmpty())
                return refuse();
            gone.append(held->id);
            goneSet.insert(held->id);
        }
        if (!moving)
            continue;
        if (!began.contains(moving->id)) {
            began.insert(moving->id, { moving->path, moving->missing });
            order.append(moving->id);
        }
        // missing is settled below, once every move is done
        if (!d->store.setPath(moving->id, to, moving->missing))
            return refuse();
    }
    QList<qint64> touched;
    QHash<QString, QString> oldToNew;
    for (const qint64 id : std::as_const(order)) {
        if (goneSet.contains(id))
            continue;
        const std::optional<LibraryTrack> now = d->store.byId(id);
        if (!now)
            continue;
        // L-MISSING: by whether a file is at the path now.
        const bool missing = !QFileInfo::exists(now->path);
        if (missing != now->missing && !d->store.setPath(id, now->path, missing))
            return refuse();
        const Start &was = began.value(id);
        if (now->path == was.path && missing == was.missing)
            continue; // it ended where it began
        touched.append(id);
        if (now->path != was.path)
            oldToNew.insert(was.path, now->path);
    }
    if (!d->store.commitBatch())
        return refuse();
    if (!gone.isEmpty())
        emit tracksRemoved(gone);
    if (!touched.isEmpty())
        emit tracksUpdated(touched);
    if (!oldToNew.isEmpty())
        emit pathsMoved(oldToNew);
    return static_cast<int>(oldToNew.size());
}

// L-WAIT: how long a change waits for another program's write lock.
void Library::setWriteWait(std::chrono::milliseconds wait)
{
    d->writeWait = wait;
    d->store.setWriteWait(wait);
}

// L-WAIT: how long a play or a skip waits for another program's write lock.
void Library::setQuickWait(std::chrono::milliseconds wait)
{
    d->quickWait = wait;
}

} // namespace linuxbee
