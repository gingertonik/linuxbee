#pragma once
#include <QList>
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

    std::optional<LibraryTrack> byId(qint64 id) const;

    // The track stored at this exact path, if there is one.
    std::optional<LibraryTrack> byPath(const QString &path) const;

    // Every track in ascending order of path. An empty category means all.
    QList<LibraryTrack> all(const QString &category) const;

    // L-SCAN: a whole scan is one transaction. SQLite writes a journal per
    // statement otherwise, which costs a hundred times over a folder.
    bool beginBatch();
    bool commitBatch();

    // L-SCAN, L-ADD: store one track and return its id. Ids are positive,
    // grow, and are never used again, so AUTOINCREMENT is the point.
    // Returns -1 when the row did not go in.
    qint64 insert(const LibraryTrack &track);

private:
    // Closes and removes this store's own connection, which Qt keys by name.
    // The destructor is the only caller: a library is open for as long as the
    // Library object lives.
    void close();

    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace linuxbee::library
