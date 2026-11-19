#include "playliststore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>

#include <sys/stat.h>
#include <unistd.h>

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
QStringList locationsOf(const QByteArray &data)
{
    QStringList out;
    for (const QString &line : playlists::decodeLines(data, PlaylistFormat::M3u8)) {
        if (!line.isEmpty())
            out.append(line);
    }
    return out;
}

// Y-EDIT, "Not a file": only a file is opened. A pipe or a device with a playlist's name is not,
// because opening a pipe waits for ever. A link to a file is the file. This is asked before the
// opener, so that an opener cannot open the pipe either.
bool isFile(const QString &path)
{
    return QFileInfo(path).isFile();
}

QStringList readLocations(const QString &path)
{
    if (!isFile(path))
        return { };
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return { };
    const QByteArray data = file.readAll();
    file.close();
    return locationsOf(data);
}

// Y-EDIT, one read, and a whole one: the file is opened (through the opener, or itself when none
// is given) and read in a loop to its end. A read that fails is a failure, not an end, and the
// count read must be the size the file had when it was opened. The answer is the bytes, or
// nothing when they could not be had. An opener that answers nothing means "could not be opened":
// the file is not tried then.
std::optional<QByteArray> readWhole(const PlaylistStore::FileOpener &opener, const QString &path)
{
    if (!isFile(path))
        return std::nullopt;
    std::unique_ptr<QIODevice> device;
    if (opener) {
        device = opener(path);
    } else {
        auto file = std::make_unique<QFile>(path);
        if (file->open(QIODevice::ReadOnly))
            device = std::move(file);
    }
    if (!device || !device->isOpen())
        return std::nullopt;
    const qint64 expected = device->size();
    QByteArray data;
    char chunk[16384];
    for (;;) {
        const qint64 got = device->read(chunk, sizeof chunk);
        if (got < 0)
            return std::nullopt;
        if (got == 0)
            break;
        data.append(chunk, got);
    }
    if (data.size() != expected)
        return std::nullopt;
    return data;
}

// Y-EDIT: empty when bytes that were read can be edited, otherwise why not. The bytes are judged;
// nothing is decoded for use here. Nothing at all means the read did not give them.
QString problemWith(const std::optional<QByteArray> &read)
{
    if (!read)
        return QStringLiteral("the file could not be read");
    const QByteArray &data = *read;
    const QString notUtf8 = QStringLiteral("the file is not UTF-8");
    // A UTF-16 byte order mark, either end first.
    if (data.startsWith(QByteArrayLiteral("\xFF\xFE"))
        || data.startsWith(QByteArrayLiteral("\xFE\xFF")))
        return notUtf8;
    // A zero byte anywhere.
    if (data.contains('\0'))
        return notUtf8;
    // Decode and encode again: a byte sequence that is not UTF-8, a letter cut off at the end
    // included, comes back as U+FFFD and so as other bytes. A UTF-8 byte order mark is set aside.
    const QByteArray body = data.startsWith(QByteArrayLiteral("\xEF\xBB\xBF")) ? data.mid(3) : data;
    if (QString::fromUtf8(body).toUtf8() != body)
        return notUtf8;
    return { };
}

enum class Written { Failed, Unchanged, Written, Changed };

// Y-SAME: what the system says of a name at one moment: which file it is, its size, and the
// times of its contents and of its record. A name that is not there is a look too.
struct Look {
    bool there = false;
    quint64 device = 0;
    quint64 number = 0;
    qint64 size = 0;
    qint64 contentsSeconds = 0;
    qint64 contentsNanoseconds = 0;
    qint64 recordSeconds = 0;
    qint64 recordNanoseconds = 0;

    bool operator==(const Look &other) const
    {
        return there == other.there && device == other.device && number == other.number
            && size == other.size && contentsSeconds == other.contentsSeconds
            && contentsNanoseconds == other.contentsNanoseconds
            && recordSeconds == other.recordSeconds && recordNanoseconds == other.recordNanoseconds;
    }
};

Look lookAt(const QString &path)
{
    Look look;
    struct stat info { };
    if (::stat(QFile::encodeName(path).constData(), &info) != 0)
        return look;
    look.there = true;
    look.device = static_cast<quint64>(info.st_dev);
    look.number = static_cast<quint64>(info.st_ino);
    look.size = static_cast<qint64>(info.st_size);
    look.contentsSeconds = static_cast<qint64>(info.st_mtim.tv_sec);
    look.contentsNanoseconds = static_cast<qint64>(info.st_mtim.tv_nsec);
    look.recordSeconds = static_cast<qint64>(info.st_ctim.tv_sec);
    look.recordNanoseconds = static_cast<qint64>(info.st_ctim.tv_nsec);
    return look;
}

// Files are written whole and then moved into place (QSaveFile), so a crash
// cannot leave half a playlist. Nothing is written when the file already
// holds exactly this, so a call that changes nothing changes nothing.
// `asRead` is what the file held when the caller read it: the one read of the call, so the
// comparison does not read the file again.
// Y-SAME: `first` is the look taken before that read. With one, the new bytes are written beside
// the file, flushed, the hook is called, and a second look is taken straight before the
// replacing; a name that is gone or a look that differs replaces nothing (Changed).
Written writeBytesOver(const QString &path, const QByteArray &data,
    const std::optional<QByteArray> &asRead, const std::optional<Look> &first,
    const PlaylistStore::BeforeReplace &hook)
{
    if (asRead && *asRead == data)
        return Written::Unchanged;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return Written::Failed;
    if (file.write(data) != data.size()) {
        file.cancelWriting();
        return Written::Failed;
    }
    if (first) {
        // The bytes are in the file beside, on disk, before the hook; the commit then has
        // nothing left to wait for between the second look and the replacing.
        if (!file.flush()) {
            file.cancelWriting();
            return Written::Failed;
        }
        if (file.handle() >= 0)
            ::fsync(file.handle());
        if (hook)
            hook(path);
        // Nothing may stand between this look and commit().
        if (!(lookAt(path) == *first)) {
            file.cancelWriting();
            return Written::Changed;
        }
    }
    if (!file.commit())
        return Written::Failed;
    return Written::Written;
}

Written writeLocationsOver(const QString &path, const QStringList &locations,
    const std::optional<QByteArray> &asRead, const std::optional<Look> &first,
    const PlaylistStore::BeforeReplace &hook)
{
    QByteArray data;
    for (const QString &location : locations)
        data += location.toUtf8() + '\n';
    return writeBytesOver(path, data, asRead, first, hook);
}

// For the callers that did not read the file for editing (create): as it was, the file is read
// here for the comparison. No looks are taken: create is not under Y-SAME.
Written writeLocations(const QString &path, const QStringList &locations)
{
    std::optional<QByteArray> asIs;
    QFile current(path);
    if (current.open(QIODevice::ReadOnly)) {
        asIs = current.readAll();
        current.close();
    }
    return writeLocationsOver(path, locations, asIs, std::nullopt, { });
}

const QString changedLine = QStringLiteral("the file was changed by another program");
const QString unwrittenLine = QStringLiteral("the file could not be written");

}

PlaylistStore::PlaylistStore(const QString &folder, QObject *parent)
    : QObject(parent)
    , m_folder(cleanRoot(folder))
{
}

void PlaylistStore::setFileOpener(FileOpener opener)
{
    m_opener = std::move(opener);
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

// Y-EDIT: empty when the entries can be changed, otherwise why not. The file is read once and
// whole, and its bytes are judged.
QString PlaylistStore::editProblem(const QString &id) const
{
    if (!exists(id))
        return QStringLiteral("there is no such playlist");
    return problemWith(readWhole(m_opener, fileFor(m_folder, id)));
}

bool PlaylistStore::setTracks(const QString &id, const QStringList &paths)
{
    m_editProblem.clear();
    if (!exists(id)) {
        m_editProblem = QStringLiteral("there is no such playlist");
        return false;
    }
    const Look first = lookAt(fileFor(m_folder, id));
    const std::optional<QByteArray> read = readWhole(m_opener, fileFor(m_folder, id));
    m_editProblem = problemWith(read);
    if (!m_editProblem.isEmpty())
        return false;
    const Written written
        = writeLocationsOver(fileFor(m_folder, id), paths, read, first, m_beforeReplace);
    if (written == Written::Changed) {
        m_editProblem = changedLine;
        return false;
    }
    if (written == Written::Failed) {
        m_editProblem = unwrittenLine;
        return false;
    }
    if (written == Written::Written)
        emit tracksChanged(id);
    return true;
}

int PlaylistStore::append(const QString &id, const QStringList &paths, bool allowDuplicates)
{
    m_editProblem.clear();
    if (!exists(id)) {
        m_editProblem = QStringLiteral("there is no such playlist");
        return 0;
    }
    const Look first = lookAt(fileFor(m_folder, id));
    const std::optional<QByteArray> read = readWhole(m_opener, fileFor(m_folder, id));
    m_editProblem = problemWith(read);
    if (!m_editProblem.isEmpty())
        return 0;
    QStringList all = locationsOf(*read);
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
    const Written written
        = writeLocationsOver(fileFor(m_folder, id), all, read, first, m_beforeReplace);
    if (written == Written::Changed) {
        m_editProblem = changedLine;
        return 0;
    }
    if (written == Written::Failed) {
        m_editProblem = unwrittenLine;
        return 0;
    }
    emit tracksChanged(id);
    return added;
}

int PlaylistStore::removeAt(const QString &id, const QList<int> &positions)
{
    m_editProblem.clear();
    if (!exists(id)) {
        m_editProblem = QStringLiteral("there is no such playlist");
        return 0;
    }
    const Look first = lookAt(fileFor(m_folder, id));
    const std::optional<QByteArray> read = readWhole(m_opener, fileFor(m_folder, id));
    m_editProblem = problemWith(read);
    if (!m_editProblem.isEmpty())
        return 0;
    const QStringList all = locationsOf(*read);
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
    const Written written
        = writeLocationsOver(fileFor(m_folder, id), kept, read, first, m_beforeReplace);
    if (written == Written::Changed) {
        m_editProblem = changedLine;
        return 0;
    }
    if (written == Written::Failed) {
        m_editProblem = unwrittenLine;
        return 0;
    }
    emit tracksChanged(id);
    return all.size() - kept.size();
}

bool PlaylistStore::moveTrack(const QString &id, int from, int to)
{
    m_editProblem.clear();
    if (!exists(id)) {
        m_editProblem = QStringLiteral("there is no such playlist");
        return false;
    }
    const Look first = lookAt(fileFor(m_folder, id));
    const std::optional<QByteArray> read = readWhole(m_opener, fileFor(m_folder, id));
    m_editProblem = problemWith(read);
    if (!m_editProblem.isEmpty())
        return false;
    QStringList all = locationsOf(*read);
    // The entry ends up at the position given; a position outside the list fails.
    if (from < 0 || from >= all.size() || to < 0 || to >= all.size())
        return false;
    if (from == to)
        return true; // nowhere to go: nothing changes
    const QString moved = all.takeAt(from);
    all.insert(to, moved);
    const Written written
        = writeLocationsOver(fileFor(m_folder, id), all, read, first, m_beforeReplace);
    if (written == Written::Changed) {
        m_editProblem = changedLine;
        return false;
    }
    if (written == Written::Failed) {
        m_editProblem = unwrittenLine;
        return false;
    }
    emit tracksChanged(id);
    return true;
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
    m_passedOver.clear(); // Y-RELINK: the list starts empty at every call
    if (oldToNew.isEmpty())
        return 0;
    int written = 0;
    const QList<PlaylistInfo> lists = playlists();
    for (const PlaylistInfo &info : lists) {
        const QString path = fileFor(m_folder, info.id);
        // Y-SAME: the first look, before the one read of this playlist.
        const Look first = lookAt(path);
        // The one read of this playlist, whole; one that was not is passed over.
        const std::optional<QByteArray> read = readWhole(m_opener, path);
        if (!read) {
            m_passedOver.append(info.id);
            continue;
        }
        const QByteArray &data = *read;

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

        if (writeBytesOver(path, out, read, first, m_beforeReplace) != Written::Written) {
            m_passedOver.append(info.id);
            continue;
        }
        ++written;
        emit tracksChanged(info.id);
    }
    return written;
}

}
