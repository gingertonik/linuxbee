#pragma once
// The library's own state, shared by the two files that implement it
// (library.cpp and scan.cpp). Not a public header: nothing includes it from
// outside src/core/library/.
#include <chrono>
#include <memory>

#include <QDir>
#include <QFileInfo>
#include <QString>

#include "library/library.h"
#include "library/store.h"

namespace linuxbee {

// L-STORE: a path is stored absolute and clean, so every lookup and every
// stored path goes through this one function. An empty path stays empty.
inline QString libraryPath(const QString &path)
{
    if (path.isEmpty())
        return QString();
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

// L-DELETED: removes the deleted rows whose deleting lies thirty days before `now` or longer ago
// and at whose path nothing is (asked of the disk once for each such row; a link counts as
// something). Returns how many; -1 when the database refused, with its words in store.lastError().
// With nothing to remove no lock is asked for. Inside a batch it is part of the batch.
int expireDeletedRows(library::Store &store, const QDateTime &now);

// The library is one open store: the file's own SQLite connection, and
// nothing else. L-OPEN keeps it open for the life of the Library object.
struct Library::Private {
    library::Store store;
    // L-COUNT: every track this object has handed out since it was opened.
    qint64 tracksHandedOut = 0;
    // L-WAIT: how long a command waits (5 seconds until set), and how long a play or a skip
    // waits (a quarter of a second until set). The store holds one at a time.
    std::chrono::milliseconds writeWait { 5000 };
    std::chrono::milliseconds quickWait { 250 };
    // L-SCHEMA: what the opening did to a file of an older schema; all empty when no step ran.
    SchemaStep step;
};

} // namespace linuxbee
