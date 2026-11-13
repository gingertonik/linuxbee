#include "playliststore.h"

// Stub. Plan 15 replaces every body in this file.
namespace linuxbee {

PlaylistStore::PlaylistStore(const QString &folder, QObject *parent)
    : QObject(parent)
    , m_folder(folder)
{
}

QString PlaylistStore::folder() const
{
    return m_folder;
}

QList<PlaylistInfo> PlaylistStore::playlists() const
{
    return { };
}

QStringList PlaylistStore::folders() const
{
    return { };
}

bool PlaylistStore::exists(const QString &id) const
{
    Q_UNUSED(id);
    return false;
}

QString PlaylistStore::create(const QString &name, const QString &inFolder, QString *error)
{
    Q_UNUSED(name);
    Q_UNUSED(inFolder);
    Q_UNUSED(error);
    return { };
}

QString PlaylistStore::rename(const QString &id, const QString &newName, QString *error)
{
    Q_UNUSED(id);
    Q_UNUSED(newName);
    Q_UNUSED(error);
    return { };
}

QString PlaylistStore::moveToFolder(const QString &id, const QString &newFolder, QString *error)
{
    Q_UNUSED(id);
    Q_UNUSED(newFolder);
    Q_UNUSED(error);
    return { };
}

bool PlaylistStore::remove(const QString &id)
{
    Q_UNUSED(id);
    return false;
}

bool PlaylistStore::createFolder(const QString &folder, QString *error)
{
    Q_UNUSED(folder);
    Q_UNUSED(error);
    return false;
}

QStringList PlaylistStore::tracks(const QString &id) const
{
    Q_UNUSED(id);
    return { };
}

bool PlaylistStore::setTracks(const QString &id, const QStringList &paths)
{
    Q_UNUSED(id);
    Q_UNUSED(paths);
    return false;
}

int PlaylistStore::append(const QString &id, const QStringList &paths, bool allowDuplicates)
{
    Q_UNUSED(id);
    Q_UNUSED(paths);
    Q_UNUSED(allowDuplicates);
    return 0;
}

int PlaylistStore::removeAt(const QString &id, const QList<int> &positions)
{
    Q_UNUSED(id);
    Q_UNUSED(positions);
    return 0;
}

bool PlaylistStore::moveTrack(const QString &id, int from, int to)
{
    Q_UNUSED(id);
    Q_UNUSED(from);
    Q_UNUSED(to);
    return false;
}

QString PlaylistStore::validateName(const QString &name)
{
    Q_UNUSED(name);
    return { };
}

}
