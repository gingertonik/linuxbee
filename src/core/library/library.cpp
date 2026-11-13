#include "library.h"

#include "store.h"

namespace linuxbee {

// The library's state is one open store. No SQL type reaches this file.
struct Library::Private {
    library::Store store;
};

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

ScanReport Library::scan(const QStringList &folders, const ScanOptions &options)
{
    Q_UNUSED(folders);
    Q_UNUSED(options);
    return { };
}

qint64 Library::addFile(const QString &path, const QString &category, const QDateTime &now)
{
    Q_UNUSED(path);
    Q_UNUSED(category);
    Q_UNUSED(now);
    return 0;
}

int Library::count() const
{
    return d->store.count();
}

QList<LibraryTrack> Library::tracks(const QString &category) const
{
    return d->store.all(category);
}

std::optional<LibraryTrack> Library::track(qint64 id) const
{
    return d->store.byId(id);
}

std::optional<LibraryTrack> Library::trackByPath(const QString &path) const
{
    Q_UNUSED(path);
    return std::nullopt;
}

int Library::remove(const QList<qint64> &ids)
{
    Q_UNUSED(ids);
    return 0;
}

int Library::setCategory(const QList<qint64> &ids, const QString &category)
{
    Q_UNUSED(ids);
    Q_UNUSED(category);
    return 0;
}

bool Library::refresh(qint64 id)
{
    Q_UNUSED(id);
    return false;
}

bool Library::setPath(qint64 id, const QString &newPath)
{
    Q_UNUSED(id);
    Q_UNUSED(newPath);
    return false;
}

int Library::relinkPaths(const QString &oldPrefix, const QString &newPrefix)
{
    Q_UNUSED(oldPrefix);
    Q_UNUSED(newPrefix);
    return 0;
}

bool Library::recordPlay(qint64 id, const QDateTime &when)
{
    Q_UNUSED(id);
    Q_UNUSED(when);
    return false;
}

bool Library::recordSkip(qint64 id, const QDateTime &when)
{
    Q_UNUSED(id);
    Q_UNUSED(when);
    return false;
}

bool Library::setStatistics(qint64 id, const TrackStatistics &statistics)
{
    Q_UNUSED(id);
    Q_UNUSED(statistics);
    return false;
}

LibraryFieldSource::LibraryFieldSource(const LibraryTrack &track, const TrackFieldOptions &options)
    : m_track(track)
    , m_fields(track.tags, track.path, options)
{
}

std::optional<QString> LibraryFieldSource::value(const QString &fieldName) const
{
    Q_UNUSED(fieldName);
    Q_UNUSED(m_track);
    return std::nullopt;
}

QStringList libraryFieldNames()
{
    return { };
}

} // namespace linuxbee
