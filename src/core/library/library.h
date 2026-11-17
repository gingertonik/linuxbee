#pragma once
#include <QHash>
#include <QMap>
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <optional>

#include "fields/trackfields.h"
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
};

// L-PARTS: what a scan needs to know of the library.
struct ScanKnown {
    struct Entry {
        qint64 id = 0;
        qint64 sizeBytes = 0;
        QDateTime modified;
        bool missing = false;
    };
    QHash<QString, Entry> byPath; // every track, by its stored path
};

// L-PARTS: what looking at the files found; nothing is stored yet.
struct ScanFindings {
    struct Reread {
        qint64 id = 0;
        bool wasMissing = false; // then it is `found`, and not counted as updated
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
    QList<Reread> reread; // known files that differ, read again
    QList<qint64> found; // marked missing, and the file is there
    QList<qint64> nowMissing; // not marked missing, inside the folders, and the file has gone
    int unchanged = 0;
    QStringList notAudio;
    int unreadableNames = 0;
    QStringList unreadableIn;
    bool cancelled = false;
};

struct ScanFindOptions {
    std::function<bool(int done, int total)> progress; // as ScanOptions::progress
    int partSize = 0; // above 0, with `part` set: hand over every so many files
    std::function<void(const ScanFindings &part)> part;
};

// L-PARTS: looks at the files; touches no library; any thread.
ScanFindings findForScan(
    const QStringList &folders, const ScanKnown &known, const ScanFindOptions &options = { });

struct ScanReport;
// L-PARTS: the reports of parts, put together.
ScanReport addedUp(const ScanReport &a, const ScanReport &b);

class Library : public QObject {
    Q_OBJECT
public:
    // Opens the library file, creating it when it does not exist. ":memory:"
    // gives a library that lives only as long as the object. Returns null and
    // sets *error when the file cannot be used.
    static std::unique_ptr<Library> open(const QString &databasePath, QString *error = nullptr);
    ~Library() override;

    static QStringList categories(); // L-CATEGORY
    static int currentSchemaVersion();

    // L-SCAN
    ScanReport scan(const QStringList &folders, const ScanOptions &options);
    // L-PARTS: the two steps of a scan, apart.
    ScanKnown scanKnown() const;
    ScanReport applyScan(const ScanFindings &findings, const ScanOptions &options);
    // L-ADD: one file. Returns its id, or 0 when it is not a readable music file.
    qint64 addFile(const QString &path, const QString &category, const QDateTime &now);

    int count() const;
    // L-COUNT: every section and how many tracks it holds; reads no track.
    QMap<QString, int> categoryCounts() const;
    // L-COUNT: how many tracks this object has handed out since it was opened.
    qint64 tracksRead() const;
    // In ascending order of path. An empty category means all.
    QList<LibraryTrack> tracks(const QString &category = { }) const;
    std::optional<LibraryTrack> track(qint64 id) const;
    std::optional<LibraryTrack> trackByPath(const QString &path) const;

    // L-EDIT. Each returns how many tracks were changed.
    int remove(const QList<qint64> &ids);
    int setCategory(const QList<qint64> &ids, const QString &category);
    bool refresh(qint64 id); // read the file's tags again
    bool setPath(qint64 id, const QString &newPath); // the file was moved
    int relinkPaths(const QString &oldPrefix, const QString &newPrefix);

    // L-STATS
    bool recordPlay(qint64 id, const QDateTime &when);
    bool recordSkip(qint64 id, const QDateTime &when);
    // Replaces all four values, for an import from another program.
    bool setStatistics(qint64 id, const TrackStatistics &statistics);

signals:
    void tracksAdded(const QList<qint64> &ids);
    void tracksUpdated(const QList<qint64> &ids);
    void tracksRemoved(const QList<qint64> &ids);
    void failed(const QString &what); // L-FAILED

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
