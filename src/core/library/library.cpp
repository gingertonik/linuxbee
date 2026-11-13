#include "library.h"

// Stub. Plan 05 replaces every body in this file.
namespace linuxbee {

struct Library::Private { };

Library::Library() = default;
Library::~Library() = default;

std::unique_ptr<Library> Library::open(const QString &databasePath, QString *error)
{
    Q_UNUSED(databasePath);
    if (error)
        *error = QStringLiteral("not implemented");
    return nullptr;
}

QStringList Library::categories()
{
    return { };
}

int Library::currentSchemaVersion()
{
    return 0;
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
    return 0;
}

QList<LibraryTrack> Library::tracks(const QString &category) const
{
    Q_UNUSED(category);
    return { };
}

std::optional<LibraryTrack> Library::track(qint64 id) const
{
    Q_UNUSED(id);
    return std::nullopt;
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

}
