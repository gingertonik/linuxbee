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

} // namespace

ScanReport Library::scan(const QStringList &folders, const ScanOptions &options)
{
    ScanReport report;
    // L-SCAN: a scan with an invalid "now" or an unknown category does nothing.
    if (!options.now.isValid() || !categories().contains(options.category))
        return report;

    const QStringList files = filesToScan(folders);
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
        // L-SCAN: a known file whose size and time are as stored is unchanged,
        // and is not read. A known file whose size or time differs is read
        // again, and a known track marked missing whose file is there is found;
        // both keep their id, category, date added and statistics.
        if (info.size() == known->sizeBytes && fileModified(info) == known->modified)
            ++report.unchanged;
    }
    // L-SCAN: after the files, every known track that is not missing, lies
    // inside one of the scanned folders, and has no file left is marked
    // missing. A track outside the scanned folders is not looked at, and a
    // cancelled scan leaves this out.
    d->store.commitBatch();

    // L-SIGNALS: the whole call emits one tracksAdded, carrying every id.
    if (!report.added.isEmpty())
        emit tracksAdded(report.added);
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
