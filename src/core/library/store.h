#pragma once
#include <QList>
#include <QMap>
#include <QString>

#include <memory>
#include <optional>

#include "library/library.h"

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
    bool open(const QString &databasePath, QString *error);

    // L-STORE: how many tracks are stored, the missing ones included.
    int count() const;

    // L-COUNT: how many tracks each category holds, the missing ones included,
    // from one counting query. Reads no track. Empty when the store is closed.
    QMap<QString, int> categoryCounts() const;

    std::optional<LibraryTrack> byId(qint64 id) const;

    // L-PARTS: every track by its stored path with only what a scan compares:
    // id, size, modification time and the missing mark. One light query, no
    // tags, and no track is built.
    ScanKnown scanKnown() const;

    // L-PARTS: what a scan compares (path, size, modification time and the
    // missing mark) for these ids only, by id. One light query per few
    // hundred ids: no tags, no track is built, and the rest of the library is
    // not read. An id the store does not hold has no entry.
    QHash<qint64, ScanFindings::Judged> heldById(const QList<qint64> &ids) const;

    // The track stored at this exact path, if there is one.
    std::optional<LibraryTrack> byPath(const QString &path) const;

    // Every track in ascending order of path. An empty category means all.
    QList<LibraryTrack> all(const QString &category) const;

    // L-SCAN: a whole scan is one transaction. SQLite writes a journal per
    // statement otherwise, which costs a hundred times over a folder.
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

private:
    // Closes and removes this store's own connection, which Qt keys by name.
    // The destructor is the only caller: a library is open for as long as the
    // Library object lives.
    void close();

    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace linuxbee::library
