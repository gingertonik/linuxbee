#include "playlists/autoplayliststore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include "playlists/playliststore.h"

// AN-STORE: one folder, one file per auto-playlist, named NAME.lbauto, holding
// the text of A-JSON in UTF-8. Every call reads the folder, so a file put there
// by another program shows up on the next call.
namespace linuxbee {

namespace {

QString quoted(const QString &name)
{
    return QLatin1Char('"') + name + QLatin1Char('"');
}

QString notThere(const QString &name)
{
    return QStringLiteral("there is no auto-playlist named ") + quoted(name);
}

QString taken(const QString &other)
{
    return QStringLiteral("an auto-playlist named ") + quoted(other)
        + QStringLiteral(" is already there");
}

QString fileFor(const QString &folder, const QString &name)
{
    return QDir(folder).filePath(name + QStringLiteral(".lbauto"));
}

void setError(QString *error, const QString &reason)
{
    if (error)
        *error = reason;
}

}

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
    const QString ending = QStringLiteral(".lbauto");
    QStringList found;
    const QFileInfoList entries
        = QDir(m_folder).entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Unsorted);
    for (const QFileInfo &entry : entries) {
        const QString file = entry.fileName();
        if (!file.endsWith(ending))
            continue;
        const QString name = file.chopped(ending.size());
        if (PlaylistStore::validateName(name).isEmpty())
            found.append(name);
    }
    found.sort(Qt::CaseInsensitive);
    return found;
}

bool AutoPlaylistStore::exists(const QString &name) const
{
    return names().contains(name);
}

bool AutoPlaylistStore::load(const QString &name, AutoPlaylist *playlist, QString *error) const
{
    if (!exists(name)) {
        setError(error, notThere(name));
        return false;
    }
    QFile file(fileFor(m_folder, name));
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("the file could not be read"));
        return false;
    }
    const QString json = QString::fromUtf8(file.readAll());
    file.close();
    AutoPlaylist loaded;
    QString why;
    if (!autoPlaylistFromJson(json, &loaded, &why)) {
        setError(error, why);
        return false;
    }
    *playlist = loaded;
    return true;
}

bool AutoPlaylistStore::save(const QString &name, const AutoPlaylist &playlist, QString *error)
{
    const QString refused = PlaylistStore::validateName(name);
    if (!refused.isEmpty()) {
        setError(error, refused);
        return false;
    }
    bool replacing = false;
    for (const QString &other : names()) {
        if (other == name) {
            replacing = true;
        } else if (other.compare(name, Qt::CaseInsensitive) == 0) {
            setError(error, taken(other));
            return false;
        }
    }
    if (!QDir().mkpath(m_folder)) {
        setError(error, QStringLiteral("the folder could not be made"));
        return false;
    }
    // QSaveFile writes beside the target and renames it into place on commit,
    // so a failure leaves the old file whole and no stray temporary file.
    const QByteArray data = autoPlaylistToJson(playlist).toUtf8();
    QSaveFile file(fileFor(m_folder, name));
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        setError(error, QStringLiteral("the file could not be written"));
        return false;
    }
    if (replacing)
        emit changed(name);
    else
        emit listChanged();
    return true;
}

bool AutoPlaylistStore::rename(const QString &name, const QString &newName, QString *error)
{
    if (!exists(name)) {
        setError(error, notThere(name));
        return false;
    }
    const QString refused = PlaylistStore::validateName(newName);
    if (!refused.isEmpty()) {
        setError(error, refused);
        return false;
    }
    if (newName == name)
        return true;
    for (const QString &other : names()) {
        if (other != name && other.compare(newName, Qt::CaseInsensitive) == 0) {
            setError(error, taken(other));
            return false;
        }
    }
    if (!QFile::rename(fileFor(m_folder, name), fileFor(m_folder, newName))) {
        setError(error, QStringLiteral("the file could not be renamed"));
        return false;
    }
    emit listChanged();
    return true;
}

bool AutoPlaylistStore::remove(const QString &name)
{
    if (!exists(name) || !QFile::remove(fileFor(m_folder, name)))
        return false;
    emit listChanged();
    return true;
}

}
