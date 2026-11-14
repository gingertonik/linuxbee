#pragma once
#include <QObject>
#include <QString>
#include <QStringList>

#include "playlists/autoplaylist.h"

// Where auto-playlists are kept: one file each, in one folder.
// Contract: docs/spec/auto-playlist-nodes.md (AN-STORE).
namespace linuxbee {

class AutoPlaylistStore : public QObject {
    Q_OBJECT
public:
    explicit AutoPlaylistStore(const QString &folder, QObject *parent = nullptr);

    QString folder() const;
    QStringList names() const; // ascending, case ignored
    bool exists(const QString &name) const;

    // False with *error set when there is no such auto-playlist or its file
    // cannot be understood.
    bool load(const QString &name, AutoPlaylist *playlist, QString *error = nullptr) const;
    // Creates or replaces. False with *error set when the name is refused
    // or the file cannot be written.
    bool save(const QString &name, const AutoPlaylist &playlist, QString *error = nullptr);
    bool rename(const QString &name, const QString &newName, QString *error = nullptr);
    bool remove(const QString &name);

signals:
    void listChanged(); // one was created, renamed or removed
    void changed(const QString &name); // an existing one was saved again

private:
    QString m_folder;
};

}
