#pragma once
#include <QHash>
#include <QMap>
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>

#include "fields/trackfields.h"
#include "library/libraryread.h"
#include "tags/tagreader.h"

// The library: which files the user has, what their tags say, and what only
// linuxBee knows about them (when they were added, how often they were
// played). Contract: docs/spec/library.md. Stored in one SQLite file.
namespace linuxbee {

struct LibraryTrack {
    qint64 id = 0;
    QString path; // absolute, clean
    QString category; // L-CATEGORY
    qint64 sizeBytes = 0;
    QDateTime modified; // the file's modification time when last read, UTC
    QDateTime dateAdded; // UTC
    int playCount = 0;
    int skipCount = 0;
    QDateTime lastPlayed; // invalid when never played
    bool missing = false; // the file was not there at the last scan
    int pictureCount = 0;
    TrackTags tags; // fields, other, format, tagTypes, audio; no pictures
};

// What only the library knows about a track.
struct TrackStatistics {
    QDateTime dateAdded;
    int playCount = 0;
    int skipCount = 0;
    QDateTime lastPlayed; // invalid: never played
};

struct ScanOptions {
    QString category = QStringLiteral("Music"); // for files that are new
    QDateTime now; // becomes dateAdded of new files; required
    // Called with the number of files looked at so far and the total.
    // Returning false stops the scan; what was stored stays stored.
    std::function<bool(int done, int total)> progress;
    // L-WAIT: true for a scan nobody asked for (the folder monitor's): it waits the short
    // wait for the library, not the long one.
    bool nobodyWaits = false;
    // L-UNPLUGGED: true for a folder emptied on purpose (`--empty-means-gone`): neither rule
    // applies, and the tracks of an empty folder are marked missing as before.
    bool emptyMeansGone = false;
};

struct ScanReport {
    QList<qint64> added;
    QList<qint64> updated; // the file had changed and was read again
    QList<qint64> nowMissing; // known tracks whose file has gone
    QList<qint64> found; // tracks that were missing and are back
    int unchanged = 0;
    QStringList notAudio; // files with a music extension that are not audio
    int unreadableNames = 0; // L-NAMES: entries whose name is not valid UTF-8
    QStringList unreadableIn; // the folders holding them, each once, ascending
    bool cancelled = false;
    // L-PARTS: findings about a track the library had changed since it was looked at.
    int passedOver = 0;
    // L-FAILED: why nothing was stored; empty when the scan was stored.
    QString refused;
    // L-READFAIL 4: what the library knows for the scan could not be read, so no file was looked
    // at and nothing was stored (`refused` has the database's words). False for every other scan,
    // a refused storing included.
    bool startUnread = false;
    // L-FOLLOW: tracks whose file another program moved; each is at its file's path now.
    QList<qint64> followed;
    // L-LOOKIN: the nearest folder that can be seen above each track whose file could not be
    // looked at for any reason but "no such file"; each once, ascending. Those tracks are left as
    // they are.
    QStringList cannotLookIn;
    // L-UNPLUGGED: the library folders (or mount points below one) that look unplugged; each
    // once, ascending. Their tracks are left as they are.
    QStringList unplugged;
    // L-SETASIDE: the real paths of files that Organise had set aside, given back their names (also
    // in `added`, since the same scan found them), and the waiting paths of those kept because a
    // file has the name; each ascending.
    QStringList setAsidePutBack;
    QStringList setAsideKept;
};

// L-SCAN: the reader's number. Raised by one whenever what the reader makes of a file changes
// (2: Love is read; 3: the length of an Ogg file is that of its last page), so that a library
// read by an older one is read again.
inline constexpr int kReaderVersion = 3;

// L-PARTS: what a scan needs to know of the library.
struct ScanKnown {
    struct Entry {
        qint64 id = 0;
        qint64 sizeBytes = 0;
        QDateTime modified;
        bool missing = false;
        int reader = kReaderVersion; // the reader that read it
    };
    QHash<QString, Entry> byPath; // every track, by its stored path
};

// L-PARTS: what looking at the files found; nothing is stored yet.
struct ScanFindings {
    struct Reread {
        qint64 id = 0;
        bool wasMissing = false; // then it is `found`, and not counted as updated
        bool onlyTheReader = false; // as stored, and read again for the reader's number alone
        LibraryTrack reading; // path, tags, size, time, picture count
    };
    // What the library was known to hold of a track when its file was looked at.
    struct Judged {
        QString path;
        qint64 sizeBytes = 0;
        QDateTime modified;
        bool missing = false;
    };
    // L-PARTS: one entry for each id named in `reread`, `found` and `nowMissing`.
    QHash<qint64, Judged> judged;
    QList<LibraryTrack> fresh; // files not known, read: no id, category or date added yet
    // L-FOLLOW: a file not known that has the size and the time of rows that are missing or
    // gone: read, held back from `fresh`, with the ids of those rows in ascending order.
    struct MaybeMoved {
        LibraryTrack reading;
        QList<qint64> rows;
    };
    QList<MaybeMoved> maybeMoved; // carried by the last part only; none when cancelled
    QList<Reread> reread; // known files that differ, read again
    QList<qint64> found; // marked missing, and the file is there
    QList<qint64> nowMissing; // not marked missing, inside the folders, and the file has gone
    int unchanged = 0;
    QStringList notAudio;
    int unreadableNames = 0;
    QStringList unreadableIn;
    bool cancelled = false;
    // L-LOOKIN: as ScanReport::cannotLookIn; carried by the last part only.
    QStringList cannotLookIn;
    // L-UNPLUGGED: as ScanReport::unplugged; carried by the last part only.
    QStringList unplugged;
    // L-SETASIDE: as ScanReport::setAsidePutBack and setAsideKept; carried by the last part only.
    QStringList setAsidePutBack;
    QStringList setAsideKept;
};

struct ScanFindOptions {
    std::function<bool(int done, int total)> progress; // as ScanOptions::progress
    bool emptyMeansGone = false; // as ScanOptions::emptyMeansGone
    // L-SETASIDE: put back what Organise set aside before looking at any file; false for a scan
    // nobody asked for.
    bool sweep = false;
    int partSize = 0; // above 0, with `part` set: hand over every so many files
    std::function<void(const ScanFindings &part)> part;
};

// L-PARTS: looks at the files; touches no library; any thread.
ScanFindings findForScan(
    const QStringList &folders, const ScanKnown &known, const ScanFindOptions &options = { });

struct ScanReport;
// L-PARTS: the reports of parts, put together.
ScanReport addedUp(const ScanReport &a, const ScanReport &b);

// L-MOVED: one file that was moved.
struct FileMove {
    QString from;
    QString to;
};

// L-STATS recordPlays: a play or a skip that has happened, of the file at `path`.
struct PlayRecord {
    QString path;
    QDateTime when;
    bool skip = false;
};

// L-STATS mergeStatistics: what an import brings for one track.
struct StatisticsMerge {
    qint64 id;
    TrackStatistics statistics;
};

// L-SCHEMA: what the opening did to a file of an older schema. `from` and `to` are 0, and
// `copyPath` is empty, when no step ran.
struct SchemaStep {
    int from = 0;
    int to = 0;
    QString copyPath; // the library as it was before the step
};

// L-SCHEMA: the one line a program says after a step; empty when none ran.
QString schemaStepNotice(const SchemaStep &step);

class Library : public QObject {
    Q_OBJECT
public:
    // Opens the library file, creating it when it does not exist. ":memory:"
    // gives a library that lives only as long as the object. Returns null and
    // sets *error when the file cannot be used.
    static std::unique_ptr<Library> open(const QString &databasePath, QString *error = nullptr);
    // L-READ: opens an existing library to read only. Makes nothing, steps nothing, takes away
    // no deleted row, and never writes to the file. Returns null and sets *error when there is
    // no file, or the file cannot be read as it is (an older or newer schema included). The
    // library handed back is for reading: a call that changes it is refused by the database.
    static std::unique_ptr<Library> openToRead(
        const QString &databasePath, QString *error = nullptr);
    ~Library() override;

    static QStringList categories(); // L-CATEGORY
    static int currentSchemaVersion();
    SchemaStep stepAtOpening() const; // L-SCHEMA

    // L-SCAN
    ScanReport scan(const QStringList &folders, const ScanOptions &options);
    // L-PARTS: the two steps of a scan, apart.
    // Tests only: a failure reads as nothing, and nothing is said (L-READFAIL 3).
    [[deprecated("tests only: the program reads through readScanKnown")]] ScanKnown
    scanKnown() const;
    // L-READFAIL 4: what the library knows for a scan, or that it could not be read. A failure
    // is said once (L-READFAIL 5) as `failed` before this returns; the caller looks at no file
    // then. A success ends the silence.
    LibraryRead<ScanKnown> readScanKnown();
    ScanReport applyScan(const ScanFindings &findings, const ScanOptions &options);
    // L-ADD: one file. Returns its id, or 0 when it is not a readable music file.
    qint64 addFile(const QString &path, const QString &category, const QDateTime &now);

    // L-READFAIL 3: the answers the program reads through. Each gives the value or that it could
    // not be read. A failure is said once as `failed` (L-READFAIL 5) and counted in
    // failedAnswers(); tracksRead() grows only on success.
    LibraryRead<int> readCount();
    // L-COUNT: every section and how many tracks it holds (0 for an empty one); reads no track.
    LibraryRead<QMap<QString, int>> readCategoryCounts();
    // In ascending order of path. An empty category means all. All or nothing.
    LibraryRead<QList<LibraryTrack>> readTracks(const QString &category = { });
    // The track with this id, or none (a success); a look that could not be made is a failure.
    LibraryRead<std::optional<LibraryTrack>> readTrack(qint64 id);
    LibraryRead<std::optional<LibraryTrack>> readTrackByPath(const QString &path);
    // L-READFAIL 5: how many answers this object has been unable to read since it was opened,
    // said or not.
    qint64 failedAnswers() const;
    // L-COUNT: how many tracks this object has handed out since it was opened.
    qint64 tracksRead() const;

    // L-READFAIL 3: the old answers, kept for the tests. A failure reads as nothing, and nothing
    // is said or counted. The program builds with warnings as errors, so it cannot call them.
    [[deprecated("tests only: the program reads through readCount")]] int count() const;
    [[deprecated("tests only: the program reads through readCategoryCounts")]] QMap<QString, int>
    categoryCounts() const;
    [[deprecated("tests only: the program reads through readTracks")]] QList<LibraryTrack> tracks(
        const QString &category = { }) const;
    [[deprecated("tests only: the program reads through readTrack")]] std::optional<LibraryTrack>
    track(qint64 id) const;
    [[deprecated(
        "tests only: the program reads through readTrackByPath")]] std::optional<LibraryTrack>
    trackByPath(const QString &path) const;

    // L-EDIT. Each returns how many tracks were changed.
    int remove(const QList<qint64> &ids);
    // L-DELETED: the files of these tracks went to the trash at `when`. The rows are kept and
    // are no tracks until a scan finds the file again. Returns how many were tracks.
    int markDeleted(const QList<qint64> &ids, const QDateTime &when);
    // L-DELETED: removes the rows deleted thirty days before `now` or longer ago, at whose path
    // nothing is. Returns how many.
    int expireDeleted(const QDateTime &now);
    static int deletedKeptDays() { return 30; }
    int setCategory(const QList<qint64> &ids, const QString &category);
    bool refresh(qint64 id); // read the file's tags again
    bool setPath(qint64 id, const QString &newPath); // the file was moved
    void setWriteWait(std::chrono::milliseconds wait); // L-WAIT
    void setQuickWait(std::chrono::milliseconds wait); // L-WAIT: for a play or a skip
    int recordMoves(const QList<FileMove> &moves); // L-MOVED
    int relinkPaths(const QString &oldPrefix, const QString &newPrefix);

    // L-STATS
    bool recordPlay(qint64 id, const QDateTime &when);
    bool recordSkip(qint64 id, const QDateTime &when);
    // Plays and skips by path, in the order they happened, stored in one change. Returns what the
    // library refused (empty when all was stored or passed over), to be offered again.
    QList<PlayRecord> recordPlays(const QList<PlayRecord> &records);
    // Replaces all four values, for an import from another program.
    bool setStatistics(qint64 id, const TrackStatistics &statistics);
    // L-STATS: an import stored in one change, all or none: the date added given, the larger
    // count, the later play, compared by the database. Returns the tracks changed, or -1 when
    // refused (nothing stored, `failed` said once).
    int mergeStatistics(const QList<StatisticsMerge> &merges);

signals:
    void tracksAdded(const QList<qint64> &ids);
    void tracksUpdated(const QList<qint64> &ids);
    void tracksRemoved(const QList<qint64> &ids);
    void pathsMoved(const QHash<QString, QString> &oldToNew); // L-MOVED
    void failed(const QString &what); // L-FAILED
    // L-FAILED (fix-70d): a failed answer that says `failed` says this straight before it, with the
    // same words. A refused change says `failed` alone.
    void answerFailed(const QString &words);

private:
    Library();
    struct Private;
    std::unique_ptr<Private> d;
};

// L-FIELDS: a library track for a template or a rule: every field of
// TrackFieldSource plus Date Added, Date Modified, Last Played, Play Count,
// Skip Count, Category and Size.
class LibraryFieldSource : public TagSource {
public:
    explicit LibraryFieldSource(const LibraryTrack &track, const TrackFieldOptions &options = { });
    std::optional<QString> value(const QString &fieldName) const override;

private:
    const LibraryTrack &m_track;
    TrackFieldSource m_fields;
};

// The names LibraryFieldSource adds, in the order of the spec's table.
QStringList libraryFieldNames();

}
