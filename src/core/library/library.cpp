#include "library.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
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
    if (!d->store.updateReading(id, fresh))
        return false;
    emit tracksUpdated(QList<qint64> { id });
    return true;
}

// L-EDIT setPath: records that the file moved. The track keeps its id and
// everything the library knows about it.
bool Library::setPath(qint64 id, const QString &newPath)
{
    const std::optional<LibraryTrack> known = d->store.byId(id);
    if (!known)
        return false;
    const QString clean = libraryPath(newPath);
    const std::optional<LibraryTrack> other = d->store.byPath(clean);
    if (other && other->id != id)
        return false; // L-EDIT: another track has that path
    // L-MISSING: missing is set by whether a file is at the new path.
    const bool missing = !QFileInfo::exists(clean);
    // L-SIGNALS: a call that changes nothing emits nothing.
    if (known->path == clean && known->missing == missing)
        return true;
    if (!d->store.setPath(id, clean, missing))
        return false;
    emit tracksUpdated(QList<qint64> { id });
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
    QList<qint64> changed;
    for (const LibraryTrack &track : d->store.all(QString())) {
        if (!track.path.startsWith(from + QLatin1Char('/')))
            continue;
        const QString moved = to + QLatin1Char('/') + track.path.mid(from.size() + 1);
        if (d->store.byPath(moved))
            continue; // L-EDIT: a track whose new path is another's is left alone
        // L-MISSING: missing is set by whether a file is at the new path.
        if (!d->store.setPath(track.id, moved, !QFileInfo::exists(moved)))
            continue;
        changed.append(track.id);
    }
    if (!changed.isEmpty())
        emit tracksUpdated(changed);
    return static_cast<int>(changed.size());
}

// L-STATS recordPlay: the count goes up, and "last played" moves forward only.
bool Library::recordPlay(qint64 id, const QDateTime &when)
{
    // L-STATS: an unknown id or an invalid time changes nothing.
    if (!when.isValid())
        return false;
    if (!d->store.recordPlay(id, when))
        return false;
    emit tracksUpdated(QList<qint64> { id });
    return true;
}

// L-STATS recordSkip: counts a skip and changes nothing else, so `when` is
// checked, because an invalid time changes nothing, and never stored.
bool Library::recordSkip(qint64 id, const QDateTime &when)
{
    if (!when.isValid())
        return false;
    if (!d->store.recordSkip(id))
        return false;
    emit tracksUpdated(QList<qint64> { id });
    return true;
}

// L-STATS setStatistics: the library's own four facts in one go, for an import.
bool Library::setStatistics(qint64 id, const TrackStatistics &statistics)
{
    // L-STATS: refused, changing nothing, for a missing date added or a
    // negative count. An unknown id is what the store's no-row answer is.
    if (!statistics.dateAdded.isValid() || statistics.playCount < 0 || statistics.skipCount < 0)
        return false;
    if (!d->store.setStatistics(id, statistics))
        return false;
    emit tracksUpdated(QList<qint64> { id });
    return true;
}

// L-MOVED. (Stub.)
int Library::setPaths(const QList<PathMove> &)
{
    return 0;
}

} // namespace linuxbee
