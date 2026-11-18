#include "playliststore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <functional>

#include "formatdetail.h"

// Y-STORE: nothing is kept in memory. Every call reads the folder, so a file
// put there by another program shows up on the next call.
namespace linuxbee {

namespace {

QString cleanRoot(const QString &folder)
{
    return QDir::cleanPath(folder);
}

QString fileFor(const QString &root, const QString &id)
{
    return QDir(root).filePath(id + QStringLiteral(".m3u8"));
}

QString folderFor(const QString &root, const QString &folder)
{
    return folder.isEmpty() ? root : QDir(root).filePath(folder);
}

QString nameOf(const QString &id)
{
    const int slash = id.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? id : id.mid(slash + 1);
}

QString folderOf(const QString &id)
{
    const int slash = id.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? QString() : id.left(slash);
}

QString joinFolder(const QString &folder, const QString &name)
{
    return folder.isEmpty() ? name : folder + QLatin1Char('/') + name;
}

// Y-NAME for one name, and for every part of a folder path. Empty means the
// name can be used; anything else says what is wrong.
QString refuseName(const QString &name)
{
    // Y-NAME: empty or only white space.
    if (name.trimmed().isEmpty())
        return QStringLiteral("the name is empty");
    // Y-NAME: begins or ends with white space.
    if (name.at(0).isSpace() || name.at(name.size() - 1).isSpace())
        return QStringLiteral("the name begins or ends with white space");
    // Y-NAME: contains "/" or "\".
    if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')))
        return QStringLiteral("the name contains a path separator");
    // Y-NAME: begins with a dot.
    if (name.startsWith(QLatin1Char('.')))
        return QStringLiteral("the name begins with a dot");
    // Y-NAME: contains a control character (below U+0020).
    for (const QChar ch : name) {
        if (ch.unicode() < 0x0020)
            return QStringLiteral("the name contains a control character");
    }
    // Y-NAME: longer than 200 bytes in UTF-8.
    if (name.toUtf8().size() > 200)
        return QStringLiteral("the name is longer than 200 bytes in UTF-8");
    return { };
}

// A folder path is Y-NAME for each part; the top (no text) is always allowed.
QString refuseFolderParts(const QString &folder)
{
    if (folder.isEmpty())
        return { };
    const QStringList parts = folder.split(QLatin1Char('/'));
    for (const QString &part : parts) {
        const QString reason = refuseName(part);
        if (!reason.isEmpty())
            return QStringLiteral("folder: ") + reason;
    }
    return { };
}

void setError(QString **error, const QString &reason)
{
    if (error && *error)
        **error = reason;
}

bool nameIsOk(const QString &name, QString **error)
{
    const QString reason = refuseName(name);
    if (reason.isEmpty())
        return true;
    setError(error, reason);
    return false;
}

// Y-STORE: every ".m3u8" file below the store's folder, and every folder
// below it. Names that start with a dot are not seen, nor is anything
// inside such a folder.
void walk(
    const QString &path, const QString &below, QList<PlaylistInfo> *playlists, QStringList *folders)
{
    const QDir dir(path);
    const QList<QFileInfo> entries
        = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Unsorted);
    for (const QFileInfo &entry : entries) {
        const QString name = entry.fileName();
        if (name.startsWith(QLatin1Char('.')))
            continue;
        if (entry.isDir()) {
            const QString sub = joinFolder(below, name);
            folders->append(sub);
            walk(entry.absoluteFilePath(), sub, playlists, folders);
        } else if (name.endsWith(QStringLiteral(".m3u8"), Qt::CaseInsensitive)) {
            const QString id = joinFolder(below, name.left(name.size() - 5)); // ".m3u8" is 5
            // Y-STORE: an id with an invalid part names nothing.
            bool partsOk = true;
            const QStringList idParts = id.split(QLatin1Char('/'));
            for (const QString &part : idParts) {
                if (!refuseName(part).isEmpty())
                    partsOk = false;
            }
            if (partsOk)
                playlists->append(PlaylistInfo { id, name.left(name.size() - 5), folderOf(id) });
        }
    }
}

// Y-STORE: ascending order, case ignored.
template <typename T> void sortAsText(QList<T> *items, const std::function<QString(const T &)> &key)
{
    std::sort(items->begin(), items->end(),
        [&key](const T &a, const T &b) { return key(a).compare(key(b), Qt::CaseInsensitive) < 0; });
}

// Y-STORE: the store's own files hold one absolute location per line and no
// "#EXT" lines. Reading is Y-DECODE for UTF-8.
QStringList readLocations(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return { };
    const QByteArray data = file.readAll();
    file.close();
    QStringList out;
    for (const QString &line : playlists::decodeLines(data, PlaylistFormat::M3u8)) {
        if (!line.isEmpty())
            out.append(line);
    }
    return out;
}

enum class Written { Failed, Unchanged, Written };

// Files are written whole and then moved into place (QSaveFile), so a crash
// cannot leave half a playlist. Nothing is written when the file already
// holds exactly this, so a call that changes nothing changes nothing.
Written writeLocations(const QString &path, const QStringList &locations)
{
    QByteArray data;
    for (const QString &location : locations)
        data += location.toUtf8() + '\n';

    QFile current(path);
    if (current.open(QIODevice::ReadOnly)) {
        const QByteArray asIs = current.readAll();
        current.close();
        if (asIs == data)
            return Written::Unchanged;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return Written::Failed;
    if (file.write(data) != data.size() || !file.commit())
        return Written::Failed;
    return Written::Written;
}

}

PlaylistStore::PlaylistStore(const QString &folder, QObject *parent)
    : QObject(parent)
    , m_folder(cleanRoot(folder))
{
}

QString PlaylistStore::folder() const
{
    return m_folder;
}

QList<PlaylistInfo> PlaylistStore::playlists() const
{
    QList<PlaylistInfo> found;
    QStringList folders;
    walk(m_folder, QString(), &found, &folders);
    sortAsText<PlaylistInfo>(&found, [](const PlaylistInfo &info) { return info.id; });
    return found;
}

QStringList PlaylistStore::folders() const
{
    QList<PlaylistInfo> playlists;
    QStringList found;
    walk(m_folder, QString(), &playlists, &found);
    found.sort(Qt::CaseInsensitive);
    return found;
}

bool PlaylistStore::exists(const QString &id) const
{
    // Y-STORE: an id is valid part by part before any path is built from it.
    if (refuseFolderParts(id).isEmpty() && !id.isEmpty())
        return QFileInfo::exists(fileFor(m_folder, id));
    return false;
}

QString PlaylistStore::create(const QString &name, const QString &inFolder, QString *error)
{
    if (!nameIsOk(name, &error))
        return { };
    const QString folderReason = refuseFolderParts(inFolder);
    if (!folderReason.isEmpty()) {
        setError(&error, folderReason);
        return { };
    }
    const QString id = joinFolder(inFolder, name);
    // Y-STORE: a new id that differs from an existing one only by case is
    // refused, so the folder can be copied to a file system that ignores case.
    for (const PlaylistInfo &info : playlists()) {
        if (info.id.compare(id, Qt::CaseInsensitive) == 0) {
            setError(
                &error, QStringLiteral("a playlist named \"%1\" is already there").arg(info.id));
            return { };
        }
    }
    if (!QDir().mkpath(folderFor(m_folder, inFolder))) {
        setError(&error, QStringLiteral("cannot make the folder \"%1\"").arg(inFolder));
        return { };
    }
    if (writeLocations(fileFor(m_folder, id), { }) != Written::Failed) {
        emit listChanged();
        return id;
    }
    setError(&error, QStringLiteral("cannot create \"%1\"").arg(id));
    return { };
}

QString PlaylistStore::rename(const QString &id, const QString &newName, QString *error)
{
    if (!exists(id)) {
        setError(&error, QStringLiteral("no playlist named \"%1\"").arg(id));
        return { };
    }
    if (!nameIsOk(newName, &error))
        return { };
    const QString newId = joinFolder(folderOf(id), newName);
    if (newId == id)
        return id; // Y-STORE: a rename to where it already is changes nothing
    const QString source = fileFor(m_folder, id);
    const QString target = fileFor(m_folder, newId);
    for (const PlaylistInfo &info : playlists()) {
        if (info.id == id)
            continue;
        if (info.id.compare(newId, Qt::CaseInsensitive) == 0) {
            setError(
                &error, QStringLiteral("a playlist named \"%1\" is already there").arg(info.id));
            return { };
        }
    }
    if (!QFile::rename(source, target)) {
        setError(&error, QStringLiteral("cannot rename \"%1\"").arg(id));
        return { };
    }
    emit listChanged();
    return newId;
}

QString PlaylistStore::moveToFolder(const QString &id, const QString &newFolder, QString *error)
{
    if (!exists(id)) {
        setError(&error, QStringLiteral("no playlist named \"%1\"").arg(id));
        return { };
    }
    const QString folderReason = refuseFolderParts(newFolder);
    if (!folderReason.isEmpty()) {
        setError(&error, folderReason);
        return { };
    }
    const QString newId = joinFolder(newFolder, nameOf(id));
    if (newId == id)
        return id; // Y-STORE: a move to where it already is changes nothing
    const QString source = fileFor(m_folder, id);
    const QString target = fileFor(m_folder, newId);
    for (const PlaylistInfo &info : playlists()) {
        if (info.id == id)
            continue;
        if (info.id.compare(newId, Qt::CaseInsensitive) == 0) {
            setError(
                &error, QStringLiteral("a playlist named \"%1\" is already there").arg(info.id));
            return { };
        }
    }
    if (!QDir().mkpath(folderFor(m_folder, newFolder))) {
        setError(&error, QStringLiteral("cannot make the folder \"%1\"").arg(newFolder));
        return { };
    }
    if (!QFile::rename(source, target)) {
        setError(&error, QStringLiteral("cannot move \"%1\"").arg(id));
        return { };
    }
    emit listChanged();
    return newId;
}

bool PlaylistStore::remove(const QString &id)
{
    if (!exists(id))
        return false;
    if (!QFile::remove(fileFor(m_folder, id)))
        return false;
    emit listChanged();
    return true;
}

bool PlaylistStore::createFolder(const QString &folder, QString *error)
{
    const QString reason = refuseFolderParts(folder);
    if (folder.isEmpty() || !reason.isEmpty()) {
        setError(&error, reason.isEmpty() ? QStringLiteral("a folder needs a name") : reason);
        return false;
    }
    const QString path = folderFor(m_folder, folder);
    if (QFileInfo::exists(path))
        return true; // Y-STORE: the folder is there already: nothing changed
    if (!QDir().mkpath(path)) {
        setError(&error, QStringLiteral("cannot make the folder \"%1\"").arg(folder));
        return false;
    }
    emit listChanged();
    return true;
}

QStringList PlaylistStore::tracks(const QString &id) const
{
    if (!exists(id))
        return { };
    return readLocations(fileFor(m_folder, id));
}

// Y-EDIT. fix-86: to be built.
QString PlaylistStore::editProblem(const QString &id) const
{
    Q_UNUSED(id);
    return { };
}

bool PlaylistStore::setTracks(const QString &id, const QStringList &paths)
{
    if (!exists(id))
        return false;
    const Written written = writeLocations(fileFor(m_folder, id), paths);
    if (written == Written::Failed)
        return false;
    if (written == Written::Written)
        emit tracksChanged(id);
    return true;
}

int PlaylistStore::append(const QString &id, const QStringList &paths, bool allowDuplicates)
{
    if (!exists(id))
        return 0;
    QStringList all = tracks(id);
    QStringList seen = all;
    int added = 0;
    for (const QString &path : paths) {
        if (!allowDuplicates && seen.contains(path))
            continue; // the playlist has it already, or it appears twice here
        seen.append(path);
        all.append(path);
        ++added;
    }
    if (added == 0)
        return 0;
    if (writeLocations(fileFor(m_folder, id), all) != Written::Failed) {
        emit tracksChanged(id);
        return added;
    }
    return 0;
}

int PlaylistStore::removeAt(const QString &id, const QList<int> &positions)
{
    if (!exists(id))
        return 0;
    const QStringList all = tracks(id);
    QSet<int> doomed;
    for (const int position : positions) {
        if (position >= 0 && position < all.size())
            doomed.insert(position); // positions that do not exist are ignored
    }
    if (doomed.isEmpty())
        return 0;
    QStringList kept;
    for (int i = 0; i < all.size(); ++i) {
        if (!doomed.contains(i))
            kept.append(all.at(i));
    }
    if (writeLocations(fileFor(m_folder, id), kept) != Written::Failed) {
        emit tracksChanged(id);
        return all.size() - kept.size();
    }
    return 0;
}

bool PlaylistStore::moveTrack(const QString &id, int from, int to)
{
    if (!exists(id))
        return false;
    QStringList all = tracks(id);
    // The entry ends up at the position given; a position outside the list fails.
    if (from < 0 || from >= all.size() || to < 0 || to >= all.size())
        return false;
    if (from == to)
        return true; // nowhere to go: nothing changes
    const QString moved = all.takeAt(from);
    all.insert(to, moved);
    if (writeLocations(fileFor(m_folder, id), all) != Written::Failed) {
        emit tracksChanged(id);
        return true;
    }
    return false;
}

QString PlaylistStore::validateName(const QString &name)
{
    return refuseName(name);
}

// Y-RELINK: every entry is looked up once, in the list as it was, so two files
// that swap places swap. Only a line that names an old path is changed, and only
// when the file has one; the bytes of every other line, its line ends, a byte
// order mark and any blank line stay as they were, and a playlist in which
// nothing changed is not written.
int PlaylistStore::relink(const QHash<QString, QString> &oldToNew)
{
    if (oldToNew.isEmpty())
        return 0;
    int written = 0;
    const QList<PlaylistInfo> lists = playlists();
    for (const PlaylistInfo &info : lists) {
        const QString path = fileFor(m_folder, info.id);
        QFile in(path);
        if (!in.open(QIODevice::ReadOnly))
            continue;
        const QByteArray data = in.readAll();
        in.close();

        QByteArray out;
        out.reserve(data.size());
        bool changed = false;
        qsizetype start = 0;
        if (data.startsWith(QByteArrayLiteral("\xEF\xBB\xBF"))) {
            out.append(data.constData(), 3); // Y-DECODE: the mark is not a line
            start = 3;
        }
        // A line ends at "\n" or "\r", as Y-DECODE reads them; the end is kept as it is.
        const qsizetype size = data.size();
        while (start < size) {
            qsizetype end = start;
            while (end < size && data.at(end) != '\n' && data.at(end) != '\r')
                ++end;
            const QByteArray line = data.mid(start, end - start);
            const auto found = oldToNew.constFind(QString::fromUtf8(line).trimmed());
            if (!line.isEmpty() && found != oldToNew.constEnd() && found.value().toUtf8() != line) {
                out += found.value().toUtf8();
                changed = true;
            } else {
                out += line;
            }
            // the line end, one byte, copied as it was
            if (end < size)
                out += data.at(end);
            start = end + 1;
        }
        if (!changed)
            continue;

        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            continue;
        if (file.write(out) != out.size() || !file.commit())
            continue;
        ++written;
        emit tracksChanged(info.id);
    }
    return written;
}

}
