#pragma once
#include <QList>
#include <QMap>
#include <QString>

#include <chrono>
#include <memory>
#include <optional>
#include <utility>

#include "library/library.h"
#include "library/libraryread.h"

// The store: the only code in linuxBee that speaks SQL. The schema and every
// statement live in store.cpp; everything above this file works with
// LibraryTrack values. Contract: docs/spec/library.md (L-OPEN, L-SCHEMA,
// L-STORE) and the plan's rule that no SQL type appears in library.h.
namespace linuxbee::library {

class Store {
public:
    Store();
    ~Store();
    Store(const Store &) = delete;
    Store &operator=(const Store &) = delete;

    // The schema this version writes, and the schema it refuses (L-SCHEMA).
    static int schemaVersion();

    // L-OPEN: opens the file, creating it and its tables when it has neither.
    // Returns false and sets *error, leaving the file as it was, when the file
    // cannot be opened, is not a database, or holds a schema newer than this
    // program's.
    // L-SCHEMA: when `step` is given it says what a step did to a file of an older schema (the
    // versions and the place of the copy made before it), and stays as it was when none ran.
    bool open(const QString &databasePath, QString *error, SchemaStep *step = nullptr);

    // L-READ: opens an existing library to read only, through the database's read-only mode:
    // nothing is made, no step runs, no write-ahead-log switch, no write lock. Returns false and
    // sets *error when there is no file, it is not a library, its schema is older or newer than
    // this program's, or it cannot be read.
    bool openToRead(const QString &databasePath, QString *error);

    // L-STORE: how many tracks are stored, the missing ones included.
    int count() const;

    // L-COUNT: how many tracks each category holds, the missing ones included,
    // from one counting query. Reads no track. Empty when the store is closed.
    QMap<QString, int> categoryCounts() const;

    // L-READFAIL: the typed readers. Each gives the value or that the read failed, with the
    // database's words, and a read that fails part way (checked at the end of the rows as well
    // as the start) is a failed read. A closed store fails every one. These are what a change
    // looks through; the plain readers below stay for the answers (R-2, R-3).
    LibraryRead<std::optional<LibraryTrack>> readById(qint64 id) const;
    LibraryRead<std::optional<LibraryTrack>> readByPath(const QString &path) const;
    LibraryRead<QList<LibraryTrack>> readAll(const QString &category) const;
    LibraryRead<ScanKnown> readScanKnown() const;
    // L-PARTS: what a scan compares (path, size, modification time and the missing mark) for
    // these ids only, by id. One light query per few hundred ids: no tags, no track is built,
    // and the rest of the library is not read. An id the store does not hold has no entry.
    LibraryRead<QHash<qint64, ScanFindings::Judged>> readHeldById(const QList<qint64> &ids) const;
    // L-DELETED: the deleted rows whose deleting is at or before `cutoffSecs`, as (id, path), in
    // ascending order of id.
    LibraryRead<QList<std::pair<qint64, QString>>> readDeletedUntil(qint64 cutoffSecs) const;
    // L-DELETED: the id of the deleted row at this exact path, 0 when there is none.
    LibraryRead<qint64> readDeletedIdAt(const QString &path) const;

    std::optional<LibraryTrack> byId(qint64 id) const;

    // L-PARTS: every track by its stored path with only what a scan compares:
    // id, size, modification time and the missing mark. One light query, no
    // tags, and no track is built.
    ScanKnown scanKnown() const;

    // The track stored at this exact path, if there is one.
    std::optional<LibraryTrack> byPath(const QString &path) const;

    // Every track in ascending order of path. An empty category means all.
    QList<LibraryTrack> all(const QString &category) const;

    // L-WAIT: how long a change waits for another program's write lock (5 seconds until set).
    // Sets the busy wait of this library's connection; does nothing while closed.
    void setWriteWait(std::chrono::milliseconds wait);

    // L-SCAN: a whole scan is one transaction. SQLite writes a journal per
    // statement otherwise, which costs a hundred times over a folder.
    // L-WAIT: takes the write lock at once (BEGIN IMMEDIATE) and waits for it.
    bool beginBatch();
    bool commitBatch();
    // L-MOVED: gives the batch up, storing none of it. Nothing is open afterwards, also when the
    // database ended the transaction itself. Does nothing when no batch is open.
    void rollbackBatch();

    // L-SCAN, L-ADD: store one track and return its id. Ids are positive,
    // grow, and are never used again, so AUTOINCREMENT is the point.
    // Returns -1 when the row did not go in.
    qint64 insert(const LibraryTrack &track);

    // L-EDIT: the ids this call changed. An id that is unknown, or already as
    // asked for, is not in the list, which is what L-EDIT's counting is.
    QList<qint64> setCategory(const QList<qint64> &ids, const QString &category);
    QList<qint64> removeIds(const QList<qint64> &ids);
    // L-FAILED: the database's words when the last setCategory or removeIds was refused
    // (nothing changed then); empty when it went through or had nothing to do.
    QString lastError() const;
    // L-WAIT: whether the last byId or byPath could look (L-READFAIL: the typed readers say so in
    // their own answer). Empty when the look was made, whatever
    // it found; the database's words when the statement failed (it was busy, say). Separate from
    // lastError(), which a read never clears.
    QString lastReadError() const;

    // L-DELETED: marks the live tracks among `ids` deleted at `whenSecs` (whole seconds, not
    // before 1970) and missing, by pieces, in one transaction. Returns the ids it marked (a
    // deleted or unknown id is not one). L-FAILED: a refusal changes nothing, returns nothing and
    // leaves words in lastError().
    QList<qint64> markDeleted(const QList<qint64> &ids, qint64 whenSecs);
    // L-DELETED: removes the deleted rows among `ids` (a live row is never touched), by pieces,
    // in one transaction. Returns the ids removed; a refusal as for markDeleted.
    QList<qint64> removeDeleted(const QList<qint64> &ids);
    // L-DELETED: the deleted row `id` is a track again: not deleted, not missing, with this reading
    // of its file (as updateReading). False with lastError() empty: no such deleted row.
    bool restoreDeleted(qint64 id, const LibraryTrack &track);

    // L-EDIT refresh: the file was read again, so its tags, size, time and
    // picture count replace what is stored. Everything only the library knows
    // (id, category, date added, statistics) stays.
    bool updateReading(qint64 id, const LibraryTrack &track);

    // L-EDIT setPath and relinkPaths: the file is at a new path, and `missing`
    // is the flag to store. L-MISSING ties that flag to whether a file is at
    // the path, and the caller works that out.
    bool setPath(qint64 id, const QString &newPath, bool missing);

    // L-SCAN, L-MISSING: a track's file has gone, or has come back. Everything
    // the library knows about the track stays: tags, statistics, id.
    bool setMissing(qint64 id, bool missing);

    // L-STATS: one play, and "last played" moved forward only when `when` is
    // later than what is stored. False means the id is unknown.
    bool recordPlay(qint64 id, const QDateTime &when);
    // L-STATS: a skip counts and changes nothing else.
    bool recordSkip(qint64 id);
    // L-STATS: the library's own four facts in one statement, for an import.
    bool setStatistics(qint64 id, const TrackStatistics &statistics);
    // L-STATS mergeStatistics: one track, merged by the statement itself from what the row holds
    // now: the date added given when valid, the larger of each count, the later last played.
    // True when the row changed. False with lastError() empty: no such track, or nothing to
    // change; with words, the database refused.
    bool mergeStatistics(qint64 id, const TrackStatistics &statistics);

private:
    // Closes and removes this store's own connection, which Qt keys by name.
    // The destructor is the only caller: a library is open for as long as the
    // Library object lives.
    void close();

    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace linuxbee::library
