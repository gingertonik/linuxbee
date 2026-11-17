#include "library/library.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QString>
#include <QStringConverter>
#include <QStringList>
#include <QTimeZone>

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <optional>
#include <set>
#include <utility>

#include "files/musicfiles.h"
#include "library/library_p.h"
#include "library/store.h"
#include "tags/tagreader.h"

// Getting files into the library: L-SCAN and L-ADD. The folder walk is the
// same list of music files the organiser uses (O-MUSIC), and reading a file is
// what plan 03's reader does; nothing here writes to a music file.
namespace linuxbee {
namespace {

// L-STORE: the library keeps how many pictures a file has and none of their
// bytes, so the read asks for the pictures without their data.
const ReadOptions kLibraryRead { true, false };

// L-STORE: a file's modification time as the library keeps it, UTC to the
// second, which is the form the next scan compares against.
QDateTime fileModified(const QFileInfo &info)
{
    const QDateTime stamp = info.fileTime(QFileDevice::FileModificationTime);
    if (!stamp.isValid())
        return QDateTime();
    return QDateTime::fromSecsSinceEpoch(stamp.toSecsSinceEpoch(), QTimeZone::UTC);
}

// L-SCAN, L-ADD: one file read the way the library stores it: what readTags
// returned, the count of its pictures, and the file's own size and time.
// Returns false, having stored nothing, when the file is not readable as audio.
bool readAsTrack(const QString &path, LibraryTrack *track)
{
    const ReadResult result = readTags(path, kLibraryRead);
    if (result.status != ReadResult::Status::Ok)
        return false;
    const QFileInfo info(path);
    track->path = path;
    track->tags = result.tags;
    track->pictureCount = int(result.tags.pictures.size());
    track->tags.pictures.clear(); // L-STORE: the bytes are never stored
    track->sizeBytes = info.size();
    track->modified = fileModified(info);
    return true;
}

// L-SCAN: every music file in every folder, each file once, in ascending order
// of path. A folder that is not there is an empty folder, and a folder given
// twice, or inside another one, adds nothing twice.
QStringList filesToScan(const QStringList &folders)
{
    QStringList files;
    for (const QString &folder : folders)
        files += findMusicFiles(folder, true);
    files.removeDuplicates();
    files.sort();
    return files;
}

// L-SCAN: does the path lie inside this folder? A folder is a folder and not a
// piece of text, so ".../a" does not reach ".../ab/x.mp3".
bool insideFolder(const QString &path, const QString &folder)
{
    const QString clean = libraryPath(folder);
    return !clean.isEmpty() && path.startsWith(clean + QLatin1Char('/'));
}

bool insideAnyFolder(const QString &path, const QStringList &folders)
{
    for (const QString &folder : folders) {
        if (insideFolder(path, folder))
            return true;
    }
    return false;
}

// L-NAMES: a name is valid when its bytes are UTF-8, checked with a decoder
// that reports errors and not by a round trip through QString.
bool isValidUtf8(const QByteArray &name)
{
    QStringDecoder decoder(QStringDecoder::Utf8);
    [[maybe_unused]] const QString decoded = decoder(name); // without this use, no error is seen
    return !decoder.hasError();
}

// L-NAMES: what the walk has found so far. A folder is told apart by its
// device and inode, so one named or reached twice is walked once.
struct UnreadableWalk {
    std::set<std::pair<dev_t, ino_t>> visited;
    int names = 0;
    QStringList folders;
};

// L-NAMES: counts the entries of one folder whose names are not valid UTF-8,
// and walks below it the way the organiser's walk does: hidden folders and
// symbolic links are not gone into. A folder with an invalid name is counted
// by its parent and not gone into, since no path to it can be made in text.
void walkForUnreadable(const QString &folder, UnreadableWalk *walk)
{
    const QByteArray path = folder.toUtf8();
    struct stat rootInfo;
    if (::stat(path.constData(), &rootInfo) != 0 || !S_ISDIR(rootInfo.st_mode))
        return;
    if (!walk->visited.insert({ rootInfo.st_dev, rootInfo.st_ino }).second)
        return;
    DIR *dir = ::opendir(path.constData());
    if (!dir)
        return;
    int here = 0;
    while (const dirent *entry = ::readdir(dir)) {
        if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0)
            continue;
        const QByteArray name(entry->d_name);
        if (!isValidUtf8(name)) {
            ++walk->names;
            ++here;
            continue;
        }
        if (name.startsWith('.'))
            continue;
        const QByteArray childPath = path + '/' + name;
        struct stat childInfo;
        if (::lstat(childPath.constData(), &childInfo) == 0 && S_ISDIR(childInfo.st_mode))
            walkForUnreadable(folder + QLatin1Char('/') + QString::fromUtf8(name), walk);
    }
    ::closedir(dir);
    if (here > 0)
        walk->folders.append(folder);
}

// L-PARTS: what the library was known to hold of a track when it was looked
// at, put into `judged` under the track's id. `path` is the path as stored.
void judge(ScanFindings *found, const QString &path, const ScanKnown::Entry &entry)
{
    ScanFindings::Judged was;
    was.path = path;
    was.sizeBytes = entry.sizeBytes;
    was.modified = entry.modified;
    was.missing = entry.missing;
    found->judged.insert(entry.id, was);
}

// L-PARTS: what looking at one file finds, put into `found`. A file the
// library does not know is read and is `fresh`; a known file that is marked
// missing and is there is `found`, and is read again when it differs; a known
// file as stored is unchanged; one that differs is read again.
void lookAtFile(const QString &path, const ScanKnown &known, ScanFindings *found)
{
    const auto entry = known.byPath.constFind(QDir::cleanPath(path));
    if (entry == known.byPath.cend()) {
        LibraryTrack track;
        if (!readAsTrack(path, &track)) {
            found->notAudio.append(path); // L-SCAN: nothing is stored for it
            return;
        }
        found->fresh.append(track);
        return;
    }
    const QFileInfo info(path);
    const bool asStored = info.size() == entry->sizeBytes && fileModified(info) == entry->modified;
    if (entry->missing) {
        // L-SCAN: a known track marked missing whose file is there is found,
        // and is read again when it differs. It is not also counted as
        // unchanged or updated.
        found->found.append(entry->id);
        judge(found, entry.key(), *entry);
        if (!asStored) {
            ScanFindings::Reread again;
            again.id = entry->id;
            again.wasMissing = true;
            if (readAsTrack(path, &again.reading))
                found->reread.append(again);
        }
        return;
    }
    // L-SCAN: a known file whose size and time are as stored is unchanged,
    // and is not read.
    if (asStored) {
        ++found->unchanged;
        return;
    }
    // L-SCAN: a known file that differs is read again.
    ScanFindings::Reread again;
    again.id = entry->id;
    if (!readAsTrack(path, &again.reading)) {
        found->notAudio.append(path); // the file is there and is not audio
        return;
    }
    found->reread.append(again);
    judge(found, entry.key(), *entry);
}

// L-SCAN, L-PARTS: every known track that is not missing, lies inside one of
// the scanned folders, and has no file left; in ascending order of path, the
// order the store keeps (its bytes, not UTF-16 code units).
QList<qint64> goneFromFolders(
    const ScanKnown &known, const QStringList &folders, ScanFindings *found)
{
    QList<std::pair<QByteArray, qint64>> gone;
    for (auto entry = known.byPath.cbegin(); entry != known.byPath.cend(); ++entry) {
        if (entry->missing || !insideAnyFolder(entry.key(), folders))
            continue;
        if (QFileInfo::exists(entry.key()))
            continue;
        gone.append({ entry.key().toUtf8(), entry->id });
        judge(found, entry.key(), *entry);
    }
    std::sort(gone.begin(), gone.end());
    QList<qint64> ids;
    for (const auto &one : std::as_const(gone))
        ids.append(one.second);
    return ids;
}

// L-NAMES: the entries inside the scanned folders whose names are not valid
// UTF-8, each folder named once in ascending order.
void findUnreadableNames(const QStringList &folders, ScanFindings *found)
{
    UnreadableWalk walk;
    for (const QString &folder : folders) {
        const QString clean = libraryPath(folder);
        if (!clean.isEmpty())
            walkForUnreadable(clean, &walk);
    }
    walk.folders.removeDuplicates();
    walk.folders.sort();
    found->unreadableNames = walk.names;
    found->unreadableIn = walk.folders;
}

} // namespace

ScanFindings findForScan(
    const QStringList &folders, const ScanKnown &known, const ScanFindOptions &options)
{
    ScanFindings found;
    const bool inParts = options.partSize > 0 && static_cast<bool>(options.part);
    const QStringList files = filesToScan(folders);
    int sinceHandOver = 0;
    for (int index = 0; index < files.size(); ++index) {
        // L-SCAN: before each file, the number of files looked at and the
        // total. False stops the looking, and what was found stays found.
        if (options.progress && !options.progress(index, files.size())) {
            found.cancelled = true;
            break;
        }
        lookAtFile(files.at(index), known, &found);
        // L-PARTS: every partSize files looked at, hand over what was found
        // and start afresh.
        if (inParts && ++sinceHandOver == options.partSize) {
            options.part(found);
            found = ScanFindings { };
            sinceHandOver = 0;
        }
    }
    // L-SCAN, L-NAMES: a cancelled scan leaves out what has gone and claims
    // nothing about names.
    if (!found.cancelled) {
        found.nowMissing = goneFromFolders(known, folders, &found);
        findUnreadableNames(folders, &found);
    }
    return found;
}

ScanReport addedUp(const ScanReport &a, const ScanReport &b)
{
    ScanReport sum = a;
    sum.added += b.added;
    sum.updated += b.updated;
    sum.nowMissing += b.nowMissing;
    sum.found += b.found;
    sum.unchanged += b.unchanged;
    sum.notAudio += b.notAudio;
    sum.unreadableNames += b.unreadableNames;
    sum.unreadableIn += b.unreadableIn;
    sum.passedOver += b.passedOver;
    sum.cancelled = a.cancelled || b.cancelled;
    return sum;
}

ScanKnown Library::scanKnown() const
{
    return d->store.scanKnown();
}

namespace {

// L-WAIT: the storing of a scan nobody asked for waits the quick wait, and the connection goes
// back to the write wait however the storing ends, stored or refused.
struct QuickWait {
    library::Store &store;
    std::chrono::milliseconds back;
    QuickWait(
        library::Store &store, std::chrono::milliseconds quick, std::chrono::milliseconds back)
        : store(store)
        , back(back)
    {
        store.setWriteWait(quick);
    }
    ~QuickWait() { store.setWriteWait(back); }
    QuickWait(const QuickWait &) = delete;
    QuickWait &operator=(const QuickWait &) = delete;
};

}

ScanReport Library::applyScan(const ScanFindings &findings, const ScanOptions &options)
{
    ScanReport report;
    // L-SCAN: an invalid "now" or an unknown category does nothing.
    if (!options.now.isValid() || !categories().contains(options.category))
        return report;

    // L-WAIT, L-FAILED: the storing step takes the write lock first, and waits for it. A scan
    // that cannot begin, or cannot commit, stores nothing: its report names no track and says
    // why, `failed` goes out once and no change signal does.
    std::optional<QuickWait> quick;
    if (options.nobodyWaits)
        quick.emplace(d->store, d->quickWait, d->writeWait);
    const auto refuse = [this, &quick]() {
        const QString why = d->store.lastError().isEmpty()
            ? QStringLiteral("the database refused the change")
            : d->store.lastError();
        d->store.rollbackBatch();
        quick.reset(); // back to the write wait before the signal goes out
        ScanReport refused;
        refused.refused = why;
        emit failed(why);
        return refused;
    };
    if (!d->store.beginBatch())
        return refuse();

    // L-PARTS: a finding is acted on only while the library still holds the
    // track as it was when it was looked at. The four values are asked for by
    // id, once, for the ids in `judged` that a finding names: one light query
    // that reads no track and not the rest of the library. L-WAIT: this is the
    // last look, and it is taken with the lock held, so no other program can
    // change a track between the look and the storing.
    QSet<qint64> named;
    for (const qint64 id : findings.found)
        named.insert(id);
    for (const ScanFindings::Reread &again : findings.reread)
        named.insert(again.id);
    for (const qint64 id : findings.nowMissing)
        named.insert(id);
    QList<qint64> toAsk;
    for (const qint64 id : std::as_const(named)) {
        if (findings.judged.contains(id))
            toAsk.append(id);
    }
    const QHash<qint64, ScanFindings::Judged> held = d->store.heldById(toAsk);
    QSet<qint64> passed;
    auto stillFits = [&findings, &held, &passed](qint64 id) {
        const auto was = findings.judged.constFind(id);
        if (was == findings.judged.cend())
            return true; // no entry: acted on as before
        const auto now = held.constFind(id);
        const bool fits = now != held.cend() && now->path == was->path
            && now->sizeBytes == was->sizeBytes && now->modified == was->modified
            && now->missing == was->missing;
        if (!fits)
            passed.insert(id);
        return fits;
    };

    // L-SCAN: a track that was marked missing and whose file is there is
    // found. L-MISSING keeps its tags, its statistics and its id.
    for (const qint64 id : findings.found) {
        if (stillFits(id) && d->store.setMissing(id, false))
            report.found.append(id);
        else if (!d->store.lastError().isEmpty())
            return refuse(); // L-FAILED: a refused write ends the storing
    }
    // L-PARTS: a file the library holds by now is left alone.
    for (const LibraryTrack &reading : findings.fresh) {
        if (d->store.byPath(QDir::cleanPath(reading.path)))
            continue;
        LibraryTrack track = reading;
        track.category = options.category;
        track.dateAdded = options.now;
        const qint64 id = d->store.insert(track);
        if (id > 0)
            report.added.append(id);
        else if (!d->store.lastError().isEmpty())
            return refuse();
    }
    // L-SCAN: a known file that differs is read again. Its id, category, date
    // added and statistics stay, which is what updateReading leaves. One that
    // was marked missing is found, and is not also updated.
    for (const ScanFindings::Reread &again : findings.reread) {
        if (stillFits(again.id) && d->store.updateReading(again.id, again.reading)
            && !again.wasMissing)
            report.updated.append(again.id);
        else if (!d->store.lastError().isEmpty())
            return refuse();
    }
    for (const qint64 id : findings.nowMissing) {
        if (stillFits(id) && d->store.setMissing(id, true))
            report.nowMissing.append(id);
        else if (!d->store.lastError().isEmpty())
            return refuse();
    }
    report.unchanged = findings.unchanged;
    report.notAudio = findings.notAudio;
    report.unreadableNames = findings.unreadableNames;
    report.unreadableIn = findings.unreadableIn;
    report.cancelled = findings.cancelled;
    report.passedOver = int(passed.size());
    if (!d->store.commitBatch())
        return refuse(); // nothing was stored, so nothing is announced
    quick.reset(); // back to the write wait before any signal goes out

    // L-SIGNALS: at most one signal of each kind, after the changes are
    // stored: one for what was added, one for everything else that changed
    // about a track, in the order found, updated, gone missing.
    if (!report.added.isEmpty())
        emit tracksAdded(report.added);
    const QList<qint64> changed = report.found + report.updated + report.nowMissing;
    if (!changed.isEmpty())
        emit tracksUpdated(changed);
    return report;
}

ScanReport Library::scan(const QStringList &folders, const ScanOptions &options)
{
    // L-SCAN: a scan with an invalid "now" or an unknown category does nothing,
    // and looks at no file.
    if (!options.now.isValid() || !categories().contains(options.category))
        return ScanReport { };

    // L-PARTS: a scan is the two steps, the looking and then the storing.
    ScanFindOptions find;
    find.progress = options.progress;
    return applyScan(findForScan(folders, scanKnown(), find), options);
}

qint64 Library::addFile(const QString &path, const QString &category, const QDateTime &now)
{
    // L-ADD: an unknown category, an invalid "now", a file that is not there
    // and a file that is not audio all answer 0 and store nothing.
    if (!now.isValid() || !categories().contains(category))
        return 0;
    const QString clean = libraryPath(path);
    const std::optional<LibraryTrack> known = d->store.byPath(clean);
    if (known)
        return known->id; // L-ADD: the file is in already; nothing changes
    if (!QFileInfo::exists(clean))
        return 0;
    LibraryTrack track;
    if (!readAsTrack(clean, &track))
        return 0;
    track.category = category;
    track.dateAdded = now;
    const qint64 id = d->store.insert(track);
    if (id <= 0) {
        // L-FAILED: the store's words are asked for only here, right after its failed call.
        if (const QString why = d->store.lastError(); !why.isEmpty())
            emit failed(why);
        return 0;
    }
    // L-SIGNALS: one signal, after the change is stored.
    emit tracksAdded(QList<qint64> { id });
    return id;
}

} // namespace linuxbee
