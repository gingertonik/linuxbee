#include "playlists/autoplayliststore.h"

// Stub. Plan 11e replaces every body in this file.
namespace linuxbee {

AutoPlaylistStore::AutoPlaylistStore(const QString &folder, QObject *parent)
    : QObject(parent)
    , m_folder(folder)
{
}

QString AutoPlaylistStore::folder() const
{
    return m_folder;
}

QStringList AutoPlaylistStore::names() const
{
    return { };
}

bool AutoPlaylistStore::exists(const QString &name) const
{
    Q_UNUSED(name);
    return false;
}

bool AutoPlaylistStore::load(const QString &name, AutoPlaylist *playlist, QString *error) const
{
    Q_UNUSED(name);
    Q_UNUSED(playlist);
    Q_UNUSED(error);
    return false;
}

bool AutoPlaylistStore::save(const QString &name, const AutoPlaylist &playlist, QString *error)
{
    Q_UNUSED(name);
    Q_UNUSED(playlist);
    Q_UNUSED(error);
    return false;
}

bool AutoPlaylistStore::rename(const QString &name, const QString &newName, QString *error)
{
    Q_UNUSED(name);
    Q_UNUSED(newName);
    Q_UNUSED(error);
    return false;
}

bool AutoPlaylistStore::remove(const QString &name)
{
    Q_UNUSED(name);
    return false;
}

}
