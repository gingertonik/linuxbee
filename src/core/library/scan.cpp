#include "library/library.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTimeZone>

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
    const QString clean = QDir::cleanPath(folder);
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

} // namespace

ScanReport Library::scan(const QStringList &folders, const ScanOptions &options)
{
    ScanReport report;
    // L-SCAN: a scan with an invalid "now" or an unknown category does nothing.
    if (!options.now.isValid() || !categories().contains(options.category))
        return report;

    const QStringList files = filesToScan(folders);
    // L-SIGNALS: everything that changed about a track already stored, in one
    // list, so the call emits at most one tracksUpdated.
    QList<qint64> changed;
    d->store.beginBatch();
    for (int index = 0; index < files.size(); ++index) {
        // L-SCAN: before each file, the number of files looked at and the
        // total. False stops the scan, and what is stored stays stored.
        if (options.progress && !options.progress(index, files.size())) {
            report.cancelled = true;
            break;
        }
        const QString path = files.at(index);
        const std::optional<LibraryTrack> known = d->store.byPath(QDir::cleanPath(path));
        if (!known) {
            LibraryTrack track;
            if (!readAsTrack(path, &track)) {
                report.notAudio.append(path); // L-SCAN: nothing is stored for it
                continue;
            }
            track.category = options.category;
            track.dateAdded = options.now;
            const qint64 id = d->store.insert(track);
            if (id > 0)
                report.added.append(id);
            continue;
        }
        const QFileInfo info(path);
        const bool asStored
            = info.size() == known->sizeBytes && fileModified(info) == known->modified;
        if (known->missing) {
            // L-SCAN: a known track marked missing whose file is there is
            // found, and is read again when it differs. It is not also counted
            // as unchanged or updated, and L-MISSING keeps its tags, its
            // statistics and its id.
            if (d->store.setMissing(known->id, false)) {
                report.found.append(known->id);
                changed.append(known->id);
            }
            if (!asStored) {
                LibraryTrack fresh;
                if (readAsTrack(path, &fresh))
                    d->store.updateReading(known->id, fresh);
            }
            continue;
        }
        // L-SCAN: a known file whose size and time are as stored is unchanged,
        // and is not read.
        if (asStored) {
            ++report.unchanged;
            continue;
        }
        // L-SCAN: a known file that differs is read again. Its id, category,
        // date added and statistics stay, which is what updateReading leaves.
        LibraryTrack fresh;
        if (!readAsTrack(path, &fresh)) {
            report.notAudio.append(path); // the file is there and is not audio
            continue;
        }
        if (d->store.updateReading(known->id, fresh)) {
            report.updated.append(known->id);
            changed.append(known->id);
        }
    }
    // L-SCAN: after the files, every known track that is not missing, lies
    // inside one of the scanned folders, and has no file left is marked
    // missing. A track outside the scanned folders is not looked at, and a
    // cancelled scan leaves this out.
    if (!report.cancelled) {
        for (const LibraryTrack &track : d->store.all(QString())) {
            if (track.missing || !insideAnyFolder(track.path, folders))
                continue;
            if (QFileInfo::exists(track.path))
                continue;
            if (d->store.setMissing(track.id, true)) {
                report.nowMissing.append(track.id);
                changed.append(track.id);
            }
        }
    }
    d->store.commitBatch();

    // L-SIGNALS: at most one signal of each kind, after the changes are
    // stored, carrying the ids: one for what was added, one for everything
    // else that changed about a track, becoming missing or found among it.
    if (!report.added.isEmpty())
        emit tracksAdded(report.added);
    if (!changed.isEmpty())
        emit tracksUpdated(changed);
    return report;
}

qint64 Library::addFile(const QString &path, const QString &category, const QDateTime &now)
{
    // L-ADD: an unknown category, an invalid "now", a file that is not there
    // and a file that is not audio all answer 0 and store nothing.
    if (!now.isValid() || !categories().contains(category))
        return 0;
    const QString clean = QDir::cleanPath(path);
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
    // L-SIGNALS: one signal, after the change is stored.
    if (id > 0)
        emit tracksAdded(QList<qint64> { id });
    return id;
}

} // namespace linuxbee
