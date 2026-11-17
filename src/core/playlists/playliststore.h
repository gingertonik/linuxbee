#pragma once
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

// The user's own playlists: one .m3u8 file each, in a folder, with
// sub-folders as playlist folders. Contract: docs/spec/playlists.md (Y-STORE).
namespace linuxbee {

struct PlaylistInfo {
    QString id; // path below the store's folder, without extension: "Rock/Favourites"
    QString name; // "Favourites"
    QString folder; // "Rock"; empty at the top

    bool operator==(const PlaylistInfo &other) const = default;
};

class PlaylistStore : public QObject {
    Q_OBJECT
public:
    explicit PlaylistStore(const QString &folder, QObject *parent = nullptr);

    QString folder() const;
    QList<PlaylistInfo> playlists() const; // ascending by id, case ignored
    QStringList folders() const; // every sub-folder, ascending, case ignored
    bool exists(const QString &id) const;

    // Each returns the new id, or an empty text with *error set.
    QString create(const QString &name, const QString &inFolder = { }, QString *error = nullptr);
    QString rename(const QString &id, const QString &newName, QString *error = nullptr);
    QString moveToFolder(const QString &id, const QString &newFolder, QString *error = nullptr);
    bool remove(const QString &id);
    bool createFolder(const QString &folder, QString *error = nullptr);

    QStringList tracks(const QString &id) const; // absolute paths, in order
    bool setTracks(const QString &id, const QStringList &paths);
    // Returns how many were added. Without allowDuplicates, a path the
    // playlist has already (or that appears twice in `paths`) is skipped.
    int append(const QString &id, const QStringList &paths, bool allowDuplicates = false);
    // Removes the entries at these positions. Returns how many were removed.
    int removeAt(const QString &id, const QList<int> &positions);
    // Moves the entry at `from` so that it ends up at position `to`.
    bool moveTrack(const QString &id, int from, int to);
    // Y-RELINK: returns how many playlists were written.
    int relink(const QHash<QString, QString> &oldToNew);

    // Y-NAME: empty when the name can be used, otherwise what is wrong.
    static QString validateName(const QString &name);

signals:
    void listChanged(); // a playlist or folder was created, renamed, moved or removed
    void tracksChanged(const QString &id);

private:
    QString m_folder;
};

}
