#include "store.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QRandomGenerator>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimeZone>
#include <QVariant>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>

#include <optional>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace linuxbee::library {
namespace {

// L-SCHEMA: this version of the schema. A file with a lower number is brought
// up to date when opened; a higher one is refused.
constexpr int kSchemaVersionValue = 3;

// L-STORE: "all times are stored and returned in UTC, to the second", so a
// time is one integer: whole seconds since the epoch. kNoTime is a time that
// never happened, and reads back as an invalid QDateTime.
constexpr qint64 kNoTime = -1;

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

// L-WAIT: the database's words for a read that failed; never empty, so a failed look is always
// told from one that was made.
QString readWords(const QSqlQuery &query)
{
    const QString words = query.lastError().text();
    return words.isEmpty() ? QStringLiteral("the database could not be read") : words;
}

QDateTime toDateTime(qint64 secs)
{
    if (secs == kNoTime)
        return QDateTime();
    return QDateTime::fromSecsSinceEpoch(secs, QTimeZone::UTC);
}

qint64 toSecs(const QDateTime &when)
{
    return when.isValid() ? when.toSecsSinceEpoch() : kNoTime;
}

QJsonArray toJsonArray(const QStringList &values)
{
    QJsonArray array;
    for (const QString &value : values)
        array.append(value);
    return array;
}

QStringList toStringList(const QJsonArray &array)
{
    QStringList values;
    for (const QJsonValue &value : array)
        values.append(value.toString());
    return values;
}

QJsonObject mapToJson(const QMap<QString, QStringList> &map)
{
    QJsonObject object;
    for (auto entry = map.cbegin(); entry != map.cend(); ++entry)
        object.insert(entry.key(), toJsonArray(entry.value()));
    return object;
}

QMap<QString, QStringList> mapFromJson(const QJsonObject &object)
{
    QMap<QString, QStringList> map;
    for (auto entry = object.cbegin(); entry != object.cend(); ++entry)
        map.insert(entry.key(), toStringList(entry.value().toArray()));
    return map;
}

// L-STORE: the tags are kept exactly as readTags returned them (fields, other,
// unsupported, format, tagTypes, audio) as JSON text. The catalogue of field
// names is open-ended, so there is no column per field. Picture data is not
// stored; the count of pictures is a column of its own.
QString tagsToJson(const TrackTags &tags)
{
    QJsonObject audio;
    audio.insert(QStringLiteral("durationMs"), tags.audio.durationMs);
    audio.insert(QStringLiteral("bitrateKbps"), tags.audio.bitrateKbps);
    audio.insert(QStringLiteral("sampleRateHz"), tags.audio.sampleRateHz);
    audio.insert(QStringLiteral("channels"), tags.audio.channels);
    audio.insert(QStringLiteral("bitsPerSample"), tags.audio.bitsPerSample);

    QJsonObject root;
    root.insert(QStringLiteral("format"), tags.format);
    root.insert(QStringLiteral("tagTypes"), toJsonArray(tags.tagTypes));
    root.insert(QStringLiteral("audio"), audio);
    root.insert(QStringLiteral("fields"), mapToJson(tags.fields));
    root.insert(QStringLiteral("other"), mapToJson(tags.other));
    root.insert(QStringLiteral("unsupported"), toJsonArray(tags.unsupported));
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

TrackTags tagsFromJson(const QString &text)
{
    const QJsonObject root = QJsonDocument::fromJson(text.toUtf8()).object();
    const QJsonObject audio = root.value(QStringLiteral("audio")).toObject();

    TrackTags tags;
    tags.format = root.value(QStringLiteral("format")).toString();
    tags.tagTypes = toStringList(root.value(QStringLiteral("tagTypes")).toArray());
    tags.unsupported = toStringList(root.value(QStringLiteral("unsupported")).toArray());
    tags.fields = mapFromJson(root.value(QStringLiteral("fields")).toObject());
    tags.other = mapFromJson(root.value(QStringLiteral("other")).toObject());
    tags.audio.durationMs = audio.value(QStringLiteral("durationMs")).toInt();
    tags.audio.bitrateKbps = audio.value(QStringLiteral("bitrateKbps")).toInt();
    tags.audio.sampleRateHz = audio.value(QStringLiteral("sampleRateHz")).toInt();
    tags.audio.channels = audio.value(QStringLiteral("channels")).toInt();
    tags.audio.bitsPerSample = audio.value(QStringLiteral("bitsPerSample")).toInt();
    return tags;
}

// L-EDIT: the ids of a call as one bound parameter each: "id = :id0 OR
// id = :id1". The names come back so the caller can bind them after prepare().
struct IdPlaceholders {
    QString condition;
    QStringList names;
};

IdPlaceholders idPlaceholders(const QList<qint64> &ids)
{
    QStringList conditions;
    QStringList names;
    for (int index = 0; index < ids.size(); ++index) {
        const QString name = QStringLiteral(":id%1").arg(index);
        names += name;
        conditions += QStringLiteral("id = %1").arg(name);
    }
    return { conditions.join(QStringLiteral(" OR ")), names };
}

void bindIds(QSqlQuery *query, const QStringList &names, const QList<qint64> &ids)
{
    for (int index = 0; index < names.size(); ++index)
        query->bindValue(names.at(index), ids.at(index));
}

// One track, every column, in the order rowFromRead reads them. A statement,
// not data: no value is ever put into SQL text.
const QLatin1String kSelectTrack(
    "SELECT id, path, category, size_bytes, modified_secs, date_added_secs, play_count, "
    "skip_count, last_played_secs, missing, picture_count, tags_json FROM tracks");

// L-SCAN, L-ADD: one row, from a track the caller read off a file. Every value
// is bound, so no path, tag, category or time is ever part of the text.
const QLatin1String kInsertTrack(
    "INSERT INTO tracks (path, category, size_bytes, modified_secs, date_added_secs, play_count, "
    "skip_count, last_played_secs, missing, picture_count, tags_json, reader)"
    " VALUES (:path, :category, :size, :modified, :added, :plays, :skips, :lastPlayed, :missing, "
    ":pictures, :tags, :reader)");

LibraryTrack rowFromRead(const QSqlQuery &query)
{
    LibraryTrack track;
    int column = 0;
    track.id = query.value(column++).toLongLong();
    track.path = query.value(column++).toString();
    track.category = query.value(column++).toString();
    track.sizeBytes = query.value(column++).toLongLong();
    track.modified = toDateTime(query.value(column++).toLongLong());
    track.dateAdded = toDateTime(query.value(column++).toLongLong());
    track.playCount = query.value(column++).toInt();
    track.skipCount = query.value(column++).toInt();
    track.lastPlayed = toDateTime(query.value(column++).toLongLong());
    track.missing = query.value(column++).toBool();
    track.pictureCount = query.value(column++).toInt();
    track.tags = tagsFromJson(query.value(column).toString());
    return track;
}

// L-SCHEMA, "A copy before a step": a connection of its own to a file, named so that no two are
// alike. The QSqlDatabase object must be out of scope before the name is removed.
QString nextCopyConnection()
{
    static std::atomic<qint64> counter { 0 };
    return QStringLiteral("linuxbee-copy-%1").arg(++counter);
}

// A name that nothing holds: a file, a folder or a link (a dangling link included, which
// QFileInfo::exists does not count).
bool isTaken(const QString &path)
{
    const QFileInfo info(path);
    return info.isSymLink() || info.exists();
}

// The library as the database itself writes it into `target`, by `VACUUM INTO` on a connection of
// its own: the write lock of the first connection does not hold it up, and it takes along what is
// still in the write-ahead log. The place goes into the statement as text, with every ' doubled.
bool vacuumInto(const QString &source, const QString &target, QString *reason)
{
    const QString name = nextCopyConnection();
    bool made = false;
    {
        QSqlDatabase second = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        second.setDatabaseName(source);
        if (!second.open()) {
            *reason = second.lastError().text();
        } else {
            QString quoted = target;
            quoted.replace(QLatin1Char('\''), QStringLiteral("''"));
            QSqlQuery query(second);
            made = query.exec(QStringLiteral("VACUUM INTO '%1'").arg(quoted));
            if (!made)
                *reason = query.lastError().text();
            query.finish();
            second.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
    if (!made && reason->isEmpty())
        *reason = QStringLiteral("the database could not make it");
    return made;
}

// The version and the count of tracks of a file, read on a connection of its own.
bool readCopy(const QString &file, int *version, int *tracks, QString *reason)
{
    const QString name = nextCopyConnection();
    bool read = false;
    {
        QSqlDatabase copy = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        copy.setDatabaseName(file);
        if (!copy.open()) {
            *reason = copy.lastError().text();
        } else {
            QSqlQuery query(copy);
            if (query.exec(QStringLiteral("PRAGMA user_version")) && query.next()) {
                *version = query.value(0).toInt();
                query.finish();
                if (query.exec(QStringLiteral("SELECT count(*) FROM tracks")) && query.next()) {
                    *tracks = query.value(0).toInt();
                    read = true;
                }
            }
            if (!read)
                *reason = query.lastError().text();
            query.finish();
            copy.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
    if (!read && reason->isEmpty())
        *reason = QStringLiteral("the copy could not be read");
    return read;
}

// The copy's bytes to the disk before it gets its name.
bool flushToDisk(const QString &file)
{
    const int fd = ::open(QFile::encodeName(file).constData(), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    const bool flushed = ::fsync(fd) == 0;
    ::close(fd);
    return flushed;
}

// The folder's entries to the disk. EINVAL and ENOTSUP mean the disk knows no such flush, and
// that is passed over; any other failure is a failure.
bool flushFolderToDisk(const QString &folder)
{
    const int fd
        = ::open(QFile::encodeName(folder).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
        return false;
    const bool synced = ::fsync(fd) == 0;
    const int error = errno;
    ::close(fd);
    return synced || error == EINVAL || error == ENOTSUP;
}

// The words of a failed system call.
QString systemWords(int error)
{
    return QString::fromLocal8Bit(std::strerror(error));
}

// L-SCHEMA, "A copy before a step": the library as it is, kept beside it before the first step
// runs. Called with the write lock held and the version looked at again, so nothing can come
// between the copy and the step. On success `*copyPath` is the copy's place; otherwise nothing is
// left beside the library but what was there, and `*reason` says why.
bool copyBeforeStep(QSqlDatabase &database, const QString &databasePath, int found,
    QString *copyPath, QString *reason)
{
    const QFileInfo library(databasePath);
    const QString folder = library.absolutePath();

    int heldTracks = 0;
    {
        QSqlQuery counting(database);
        if (!counting.exec(QStringLiteral("SELECT count(*) FROM tracks")) || !counting.next()) {
            *reason = counting.lastError().text();
            return false;
        }
        heldTracks = counting.value(0).toInt();
        counting.finish();
    }

    // The spare name: a name that nothing holds, in the library's own folder.
    static const QString alphabet
        = QStringLiteral("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
    // The spare file is made here, new, for its owner alone (0600), before the database writes
    // into it. O_EXCL refuses a name that is there, a folder or a link included.
    QString spare;
    for (int attempt = 0; attempt < 100 && spare.isEmpty(); ++attempt) {
        QString name = folder + QStringLiteral("/.lbcopy-");
        for (int index = 0; index < 6; ++index)
            name += alphabet.at(int(QRandomGenerator::system()->bounded(alphabet.size())));
        if (isTaken(name))
            continue;
        const int fd = ::open(
            QFile::encodeName(name).constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (fd >= 0) {
            ::close(fd);
            spare = name;
        } else if (errno != EEXIST) {
            *reason = systemWords(errno);
            return false;
        }
    }
    if (spare.isEmpty()) {
        *reason = QStringLiteral("no free name for the spare file");
        return false;
    }

    // Only the spare file this call chose is ever taken away.
    const auto giveUp = [&spare] { QFile::remove(spare); };
    if (!vacuumInto(library.absoluteFilePath(), spare, reason)) {
        giveUp(); // VACUUM INTO leaves an empty file when it fails
        return false;
    }
    int copyVersion = 0;
    int copyTracks = 0;
    if (!readCopy(spare, &copyVersion, &copyTracks, reason)) {
        giveUp();
        return false;
    }
    if (copyVersion != found || copyTracks != heldTracks) {
        *reason = QStringLiteral("what was written is not the library as it is");
        giveUp();
        return false;
    }
    if (!flushToDisk(spare)) {
        *reason = QStringLiteral("it could not be flushed to the disk");
        giveUp();
        return false;
    }
    // Before the copy gets its name it has the library's mode: a link is followed, so a library
    // reached through one gives its own mode, not the link's.
    struct stat libraryStat { };
    if (::stat(QFile::encodeName(databasePath).constData(), &libraryStat) != 0) {
        *reason = systemWords(errno);
        giveUp();
        return false;
    }
    if (::chmod(QFile::encodeName(spare).constData(), libraryStat.st_mode & 0777) != 0) {
        *reason = systemWords(errno);
        giveUp();
        return false;
    }

    // The name: the library's own name and `.schema-N`, then `-2`, `-3` and so on. QFile::rename
    // does not replace, so a name taken since it was looked at is never written over.
    const QString base = library.absoluteFilePath() + QStringLiteral(".schema-%1").arg(found);
    for (int number = 1; number <= 10000; ++number) {
        const QString name = number == 1 ? base : base + QStringLiteral("-%1").arg(number);
        if (isTaken(name))
            continue;
        QFile spareFile(spare);
        if (spareFile.rename(name)) {
            // The folder is flushed once the copy has its name, before the step runs. A failure
            // takes the copy away: it is the one removing call here, and only for this copy.
            if (!flushFolderToDisk(folder)) {
                *reason = QStringLiteral("its folder could not be flushed to the disk");
                QFile::remove(name);
                return false;
            }
            *copyPath = name;
            return true;
        }
        // A name taken since it was looked at is the next one's business; any other failure ends
        // it.
        if (!isTaken(name)) {
            *reason = spareFile.errorString();
            giveUp();
            return false;
        }
    }
    *reason = QStringLiteral("no free name for the copy");
    giveUp();
    return false;
}

// L-SCHEMA: create what a version-0 file has no rows for, and take a file of version 1 or 2 up to
// version 3 (one step after the other, in the one transaction). All of it is this program's own
// text: nothing here carries a path, a tag or any other data. L-WAIT, L-OPEN: the write lock is
// taken first and waited for. Another start may have made the tables or run the step while this one
// waited, so the version is looked at again once the lock is held, and the steps run from what is
// found then; a file already at this version needs none and this succeeds. The steps and the new
// `user_version` are one transaction.
bool createSchema(
    QSqlDatabase &database, const QString &databasePath, QString *error, SchemaStep *step)
{
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("BEGIN IMMEDIATE"))) {
        setError(error,
            QStringLiteral("Cannot start writing a new library: %1").arg(query.lastError().text()));
        return false;
    }
    int found = 0;
    {
        QSqlQuery again(database);
        if (!again.exec(QStringLiteral("PRAGMA user_version")) || !again.next()) {
            const QString reason = again.lastError().text();
            again.finish();
            database.rollback();
            setError(error, QStringLiteral("Cannot read the library version: %1").arg(reason));
            return false;
        }
        found = again.value(0).toInt();
        again.finish();
    }
    if (found >= kSchemaVersionValue) {
        database.rollback();
        return true;
    }
    // L-SCHEMA: a file that holds a schema of 1 or more is copied before the first step; a new
    // file has nothing to copy. One opening makes one copy, however many steps it runs.
    QString copyPath;
    if (found >= 1) {
        QString reason;
        if (!copyBeforeStep(database, databasePath, found, &copyPath, &reason)) {
            database.rollback();
            setError(error,
                QStringLiteral(
                    "The library was not brought up to date: no copy of it could be made "
                    "in %1 (%2)")
                    .arg(QFileInfo(databasePath).absolutePath(), reason));
            return false;
        }
    }
    // The copy lies only beside a library whose step ran: the one removing call besides the spare
    // file's, and it takes away only the file this opening made.
    const auto takeCopyAway = [&copyPath] {
        if (!copyPath.isEmpty())
            QFile::remove(copyPath);
    };
    QStringList statements;
    if (found < 1) {
        statements += QStringLiteral("CREATE TABLE tracks ("
                                     "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                                     "path TEXT NOT NULL UNIQUE, "
                                     "category TEXT NOT NULL, "
                                     "size_bytes INTEGER NOT NULL, "
                                     "modified_secs INTEGER NOT NULL, "
                                     "date_added_secs INTEGER NOT NULL, "
                                     "play_count INTEGER NOT NULL DEFAULT 0, "
                                     "skip_count INTEGER NOT NULL DEFAULT 0, "
                                     "last_played_secs INTEGER NOT NULL DEFAULT (-1), "
                                     "missing INTEGER NOT NULL DEFAULT 0, "
                                     "picture_count INTEGER NOT NULL DEFAULT 0, "
                                     "tags_json TEXT NOT NULL, "
                                     "reader INTEGER NOT NULL DEFAULT 0, "
                                     "deleted_secs INTEGER NOT NULL DEFAULT (-1))");
        statements += QStringLiteral("CREATE INDEX tracks_by_category ON tracks (category)");
    } else {
        // L-SCHEMA, version 2: every track already held is given 0, lower than any reader's.
        if (found < 2)
            statements += QStringLiteral(
                "ALTER TABLE tracks ADD COLUMN reader INTEGER NOT NULL DEFAULT 0");
        // L-SCHEMA, version 3: every track already held is given -1, not deleted.
        if (found < 3)
            statements += QStringLiteral(
                "ALTER TABLE tracks ADD COLUMN deleted_secs INTEGER NOT NULL DEFAULT (-1)");
    }
    // The schema number lives in SQLite's user_version. It is this program's own number, so it is
    // written out and not bound.
    statements += QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersionValue);
    for (const QString &statement : std::as_const(statements)) {
        if (!query.exec(statement)) {
            const QString reason = query.lastError().text();
            database.rollback();
            takeCopyAway();
            setError(error,
                found < 1 ? QStringLiteral("Cannot create the library tables: %1").arg(reason)
                          : QStringLiteral("Cannot bring the library up to date: %1").arg(reason));
            return false;
        }
    }
    if (!database.commit()) {
        const QString reason = database.lastError().text();
        database.rollback();
        takeCopyAway();
        setError(error, QStringLiteral("Cannot save the new library: %1").arg(reason));
        return false;
    }
    if (step && !copyPath.isEmpty()) {
        step->from = found;
        step->to = kSchemaVersionValue;
        step->copyPath = copyPath;
    }
    return true;
}

// Opens the file and checks what it holds. The QSqlDatabase for `connection`
// is gone when this returns, which is what lets the caller remove it.
bool openAndCheckSchema(
    const QString &databasePath, const QString &connection, QString *error, SchemaStep *step)
{
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    database.setDatabaseName(databasePath);
    // L-OPEN: the folder must exist already; SQLite opens nothing without it.
    if (!database.open()) {
        setError(error,
            QStringLiteral("Cannot open the library file: %1").arg(database.lastError().text()));
        return false;
    }
    // L-OPEN: a file that is not a database opens happily in SQLite, and the
    // first statement is what says so.
    QSqlQuery version(database);
    if (!version.exec(QStringLiteral("PRAGMA user_version")) || !version.next()) {
        const QString words = version.lastError().text();
        // L-OPEN: a library that cannot be written to is not a file that is no library. The
        // database says "readonly" in its own words.
        if (words.contains(QLatin1String("readonly"), Qt::CaseInsensitive))
            setError(error,
                QStringLiteral("The library cannot be written to: %1 (%2)")
                    .arg(databasePath, words));
        else
            setError(error, QStringLiteral("That file is not a linuxBee library: %1").arg(words));
        return false;
    }
    const int found = version.value(0).toInt();
    // A statement left open on the connection stops the switch to a write-ahead log below.
    version.finish();
    if (found > kSchemaVersionValue) {
        setError(error,
            QStringLiteral("That library was written by a newer linuxBee: schema %1, "
                           "this linuxBee reads schema %2")
                .arg(found)
                .arg(kSchemaVersionValue));
        return false;
    }
    // L-OPEN: a library on disk keeps a write-ahead log, at the default synchronous. Where the
    // file system cannot (a network disk) SQLite answers with the journal it has, and the
    // library opens all the same, so the answer is not checked. A library in memory has none.
    if (!databasePath.isEmpty() && databasePath != QLatin1String(":memory:")) {
        QSqlQuery wal(database);
        if (wal.exec(QStringLiteral("PRAGMA journal_mode=WAL")))
            while (wal.next()) { }
        wal.finish();
    }
    if (found < kSchemaVersionValue && !createSchema(database, databasePath, error, step))
        return false;
    return true;
}

// L-READ: opens the file through the database's read-only mode and checks what it holds. Nothing
// is written: no step, no copy, no write-ahead-log switch, no write lock.
bool openToReadAndCheck(const QString &databasePath, const QString &connection, QString *error)
{
    const QFileInfo file(databasePath);
    // exists() follows a link, so a link that leads nowhere is no file either.
    if (!file.exists()) {
        setError(error, QStringLiteral("there is no such file"));
        return false;
    }
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    database.setDatabaseName(databasePath);
    // L-WAIT: the same five seconds a writing command waits for the write lock.
    database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=5000"));
    if (!database.open()) {
        setError(error,
            QStringLiteral("Cannot open the library file: %1").arg(database.lastError().text()));
        return false;
    }
    QSqlQuery version(database);
    if (!version.exec(QStringLiteral("PRAGMA user_version")) || !version.next()) {
        const QString words = version.lastError().text();
        // L-READ "The wait": still held after the five seconds, the database says "locked".
        if (words.contains(QLatin1String("locked"), Qt::CaseInsensitive))
            setError(
                error, QStringLiteral("the library is busy: another program holds it; try again"));
        // L-READ "Its words": a folder that cannot be written to, with no -shm beside the file.
        else if (words.contains(QLatin1String("readonly"), Qt::CaseInsensitive))
            setError(error,
                QStringLiteral("the library cannot be read here: its folder cannot be written "
                               "to, and the database needs its side files (-wal, -shm) beside it"));
        else
            setError(error, QStringLiteral("That file is not a linuxBee library: %1").arg(words));
        return false;
    }
    const int found = version.value(0).toInt();
    version.finish();
    // L-READ "Its words": a file with no tracks table is no library, whatever its version says.
    QSqlQuery tables(database);
    if (!tables.exec(QStringLiteral("SELECT count(*) FROM sqlite_master "
                                    "WHERE type = 'table' AND name = 'tracks'"))
        || !tables.next()) {
        setError(error,
            QStringLiteral("That file is not a linuxBee library: %1").arg(readWords(tables)));
        return false;
    }
    if (tables.value(0).toInt() == 0) {
        setError(error, QStringLiteral("That file is not a linuxBee library: it holds no library"));
        return false;
    }
    tables.finish();
    if (found > kSchemaVersionValue) {
        setError(error,
            QStringLiteral("That library was written by a newer linuxBee: schema %1, "
                           "this linuxBee reads schema %2")
                .arg(found)
                .arg(kSchemaVersionValue));
        return false;
    }
    if (found < kSchemaVersionValue) {
        setError(error,
            QStringLiteral("it is at schema %1 and this linuxBee reads schema %2. A command "
                           "that only reads does not bring it up to date; library scan, "
                           "library import --apply or the window does, after a copy.")
                .arg(found)
                .arg(kSchemaVersionValue));
        return false;
    }
    return true;
}

} // namespace

struct Store::Private {
    // Every open library needs its own named connection, so that several
    // libraries, including several in-memory ones, can be open at once.
    static QString nextName()
    {
        static std::atomic<qint64> counter { 0 };
        return QStringLiteral("linuxbee-library-%1").arg(++counter);
    }

    QString connection; // empty while closed
    bool opened = false;
    bool inBatch = false;
    QString lastError; // L-FAILED: the database's words for the last refused edit

    // Never empty, so the library always has words to say.
    void refused(const QString &words)
    {
        lastError = words.isEmpty() ? QStringLiteral("the database refused the change") : words;
    }
};

Store::Store()
    : d(std::make_unique<Private>())
{
}

Store::~Store()
{
    close();
}

int Store::schemaVersion()
{
    return kSchemaVersionValue;
}

void Store::close()
{
    if (d->connection.isEmpty())
        return;
    // Qt warns when a connection is removed while a QSqlDatabase object for it
    // is alive, so the object goes out of scope before removeDatabase runs.
    {
        QSqlDatabase database = QSqlDatabase::database(d->connection);
        if (database.isOpen())
            database.close();
    }
    QSqlDatabase::removeDatabase(d->connection);
    d->connection.clear();
    d->opened = false;
    d->inBatch = false;
}

bool Store::open(const QString &databasePath, QString *error, SchemaStep *step)
{
    const QString name = Private::nextName();
    if (!openAndCheckSchema(databasePath, name, error, step)) {
        QSqlDatabase::removeDatabase(name);
        return false;
    }
    d->connection = name;
    d->opened = true;
    return true;
}

bool Store::openToRead(const QString &databasePath, QString *error)
{
    const QString name = Private::nextName();
    if (!openToReadAndCheck(databasePath, name, error)) {
        QSqlDatabase::removeDatabase(name);
        return false;
    }
    d->connection = name;
    d->opened = true;
    return true;
}

// L-READFAIL: the answer of a read that could not be made, or ended in an error part way: Qt's
// SQLite driver gives a row's failure at next(), which returns false as at the end of the rows,
// so the end of the rows is checked as well as the start.
static bool endedInError(const QSqlQuery &query)
{
    return query.lastError().isValid();
}

LibraryRead<std::optional<LibraryTrack>> Store::readById(qint64 id) const
{
    using Read = LibraryRead<std::optional<LibraryTrack>>;
    if (!d->opened)
        return Read::failure(QStringLiteral("the library is not open"));
    QSqlQuery query(QSqlDatabase::database(d->connection));
    query.prepare(
        QStringLiteral("%1 WHERE id = :id AND deleted_secs < 0").arg(QLatin1String(kSelectTrack)));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec())
        return Read::failure(readWords(query));
    if (!query.next()) {
        if (endedInError(query))
            return Read::failure(readWords(query));
        return Read::ok(std::nullopt);
    }
    return Read::ok(rowFromRead(query));
}

LibraryRead<std::optional<LibraryTrack>> Store::readByPath(const QString &path) const
{
    using Read = LibraryRead<std::optional<LibraryTrack>>;
    if (!d->opened)
        return Read::failure(QStringLiteral("the library is not open"));
    QSqlQuery query(QSqlDatabase::database(d->connection));
    query.prepare(QStringLiteral("%1 WHERE path = :path AND deleted_secs < 0")
            .arg(QLatin1String(kSelectTrack)));
    query.bindValue(QStringLiteral(":path"), path);
    if (!query.exec())
        return Read::failure(readWords(query));
    if (!query.next()) {
        if (endedInError(query))
            return Read::failure(readWords(query));
        return Read::ok(std::nullopt);
    }
    return Read::ok(rowFromRead(query));
}

LibraryRead<QList<LibraryTrack>> Store::readAll(const QString &category) const
{
    using Read = LibraryRead<QList<LibraryTrack>>;
    if (!d->opened)
        return Read::failure(QStringLiteral("the library is not open"));
    QSqlQuery query(QSqlDatabase::database(d->connection));
    if (category.isEmpty()) {
        query.prepare(QStringLiteral("%1 WHERE deleted_secs < 0 ORDER BY path")
                .arg(QLatin1String(kSelectTrack)));
    } else {
        query.prepare(
            QStringLiteral("%1 WHERE category = :category AND deleted_secs < 0 ORDER BY path")
                .arg(QLatin1String(kSelectTrack)));
        query.bindValue(QStringLiteral(":category"), category);
    }
    if (!query.exec())
        return Read::failure(readWords(query));
    QList<LibraryTrack> list;
    while (query.next())
        list.append(rowFromRead(query));
    if (endedInError(query))
        return Read::failure(readWords(query));
    return Read::ok(std::move(list));
}

LibraryRead<int> Store::readCount() const
{
    using Read = LibraryRead<int>;
    if (!d->opened)
        return Read::failure(QStringLiteral("the library is not open"));
    QSqlQuery query(QSqlDatabase::database(d->connection));
    // L-DELETED: a deleted track is no track.
    if (!query.exec(QStringLiteral("SELECT count(*) FROM tracks WHERE deleted_secs < 0")))
        return Read::failure(readWords(query));
    if (!query.next())
        return Read::failure(readWords(query));
    return Read::ok(query.value(0).toInt());
}

// L-READFAIL 3: how many tracks each category holds, all or nothing. A failure at the start or
// part way through the rows is a failed read, not a shorter map.
LibraryRead<QMap<QString, int>> Store::readCategoryCounts() const
{
    using Read = LibraryRead<QMap<QString, int>>;
    if (!d->opened)
        return Read::failure(QStringLiteral("the library is not open"));
    QSqlQuery query(QSqlDatabase::database(d->connection));
    // L-DELETED: a deleted track is no track.
    if (!query.exec(QStringLiteral(
            "SELECT category, count(*) FROM tracks WHERE deleted_secs < 0 GROUP BY category")))
        return Read::failure(readWords(query));
    QMap<QString, int> counts;
    while (query.next())
        counts.insert(query.value(0).toString(), query.value(1).toInt());
    if (endedInError(query))
        return Read::failure(readWords(query));
    return Read::ok(std::move(counts));
}

LibraryRead<ScanKnown> Store::readScanKnown() const
{
    using Read = LibraryRead<ScanKnown>;
    if (!d->opened)
        return Read::failure(QStringLiteral("the library is not open"));
    QSqlQuery query(QSqlDatabase::database(d->connection));
    if (!query.exec(QStringLiteral(
            "SELECT id, path, size_bytes, modified_secs, missing, reader FROM tracks "
            "WHERE deleted_secs < 0")))
        return Read::failure(readWords(query));
    ScanKnown known;
    while (query.next()) {
        ScanKnown::Entry entry;
        entry.id = query.value(0).toLongLong();
        entry.sizeBytes = query.value(2).toLongLong();
        entry.modified = toDateTime(query.value(3).toLongLong());
        entry.missing = query.value(4).toBool();
        entry.reader = query.value(5).toInt();
        known.byPath.insert(query.value(1).toString(), entry);
    }
    if (endedInError(query))
        return Read::failure(readWords(query));
    return Read::ok(std::move(known));
}

LibraryRead<QHash<qint64, ScanFindings::Judged>> Store::readHeldById(const QList<qint64> &ids) const
{
    using Read = LibraryRead<QHash<qint64, ScanFindings::Judged>>;
    if (!d->opened)
        return Read::failure(QStringLiteral("the library is not open"));
    QHash<qint64, ScanFindings::Judged> held;
    // SQLite refuses an expression tree deeper than 1000, and the ids are an
    // OR chain, so a long list is asked for in pieces.
    constexpr qsizetype kPiece = 400;
    for (qsizetype from = 0; from < ids.size(); from += kPiece) {
        const QList<qint64> piece = ids.mid(from, kPiece);
        const IdPlaceholders placeholders = idPlaceholders(piece);
        QSqlQuery query(QSqlDatabase::database(d->connection));
        query.prepare(QStringLiteral("SELECT id, path, size_bytes, modified_secs, missing "
                                     "FROM tracks WHERE (%1) AND deleted_secs < 0")
                .arg(placeholders.condition));
        bindIds(&query, placeholders.names, piece);
        if (!query.exec())
            return Read::failure(readWords(query));
        while (query.next()) {
            ScanFindings::Judged entry;
            entry.path = query.value(1).toString();
            entry.sizeBytes = query.value(2).toLongLong();
            entry.modified = toDateTime(query.value(3).toLongLong());
            entry.missing = query.value(4).toBool();
            held.insert(query.value(0).toLongLong(), entry);
        }
        if (endedInError(query))
            return Read::failure(readWords(query));
    }
    return Read::ok(std::move(held));
}

bool Store::beginBatch()
{
    d->lastError.clear();
    if (!d->opened || d->inBatch) {
        d->refused(QString());
        return false;
    }
    // L-WAIT: the write lock is taken here, at the start, so the wait applies; a transaction that
    // reads first and writes after is refused by SQLite at once.
    QSqlQuery begin(QSqlDatabase::database(d->connection));
    if (!begin.exec(QStringLiteral("BEGIN IMMEDIATE"))) {
        d->refused(begin.lastError().text());
        return false;
    }
    d->inBatch = true;
    return true;
}

// L-WAIT: the busy wait of this connection, which every statement that has to wait for another
// program's lock uses.
void Store::setWriteWait(std::chrono::milliseconds wait)
{
    if (!d->opened)
        return;
    QSqlQuery query(QSqlDatabase::database(d->connection));
    query.exec(QStringLiteral("PRAGMA busy_timeout = %1").arg(qint64(wait.count())));
}

// L-MOVED: a commit that fails leaves no transaction open, so the next call finds nothing in its
// way.
bool Store::commitBatch()
{
    d->lastError.clear();
    if (!d->inBatch) {
        d->refused(QString());
        return false;
    }
    d->inBatch = false;
    QSqlDatabase database = QSqlDatabase::database(d->connection);
    if (database.commit())
        return true;
    d->refused(database.lastError().text());
    database.rollback();
    return false;
}

// L-MOVED: the rollback may fail when the database ended the transaction itself; that is fine,
// nothing is open then either.
void Store::rollbackBatch()
{
    if (!d->inBatch)
        return;
    d->inBatch = false;
    QSqlDatabase::database(d->connection).rollback();
}

// L-FAILED: -1 with lastError() empty means nothing was done; with words, the database refused.
qint64 Store::insert(const LibraryTrack &track)
{
    d->lastError.clear();
    if (!d->opened)
        return -1;
    QSqlQuery query(QSqlDatabase::database(d->connection));
    query.prepare(QLatin1String(kInsertTrack));
    query.bindValue(QStringLiteral(":path"), track.path);
    query.bindValue(QStringLiteral(":category"), track.category);
    query.bindValue(QStringLiteral(":size"), track.sizeBytes);
    query.bindValue(QStringLiteral(":modified"), toSecs(track.modified));
    query.bindValue(QStringLiteral(":added"), toSecs(track.dateAdded));
    query.bindValue(QStringLiteral(":plays"), track.playCount);
    query.bindValue(QStringLiteral(":skips"), track.skipCount);
    query.bindValue(QStringLiteral(":lastPlayed"), toSecs(track.lastPlayed));
    query.bindValue(QStringLiteral(":missing"), track.missing);
    query.bindValue(QStringLiteral(":pictures"), track.pictureCount);
    query.bindValue(QStringLiteral(":tags"), tagsToJson(track.tags));
    query.bindValue(QStringLiteral(":reader"), kReaderVersion);
    if (!query.exec()) {
        d->refused(query.lastError().text());
        return -1;
    }
    return query.lastInsertId().toLongLong();
}

namespace {

// L-MANY: a long list of ids is asked for in pieces (the same size readHeldById uses), because the
// database refuses an expression that names every id once it is long enough.
constexpr qsizetype kEditPiece = 400;

// L-MANY, L-FAILED: the whole call is one transaction, so a refusal in any piece takes back the
// pieces before it. A batch that is open already is the call's transaction; then a savepoint
// does the same job inside it.
class EditScope {
public:
    EditScope(QSqlDatabase database, bool inBatch)
        : m_database(database)
        , m_savepoint(inBatch)
    {
        if (m_savepoint) {
            QSqlQuery begin(m_database);
            m_open = begin.exec(QStringLiteral("SAVEPOINT linuxbee_edit"));
            if (!m_open)
                m_error = begin.lastError().text();
        } else {
            // L-WAIT: the write lock first, so the wait applies (not Qt's deferred BEGIN).
            QSqlQuery begin(m_database);
            m_open = begin.exec(QStringLiteral("BEGIN IMMEDIATE"));
            if (!m_open)
                m_error = begin.lastError().text();
        }
    }
    ~EditScope() { rollBack(); }
    EditScope(const EditScope &) = delete;
    EditScope &operator=(const EditScope &) = delete;

    bool isOpen() const { return m_open; }
    QString error() const { return m_error; }

    // Keeps the words of the first thing that went wrong.
    void fail(const QString &words)
    {
        if (m_error.isEmpty())
            m_error = words;
    }

    bool commit()
    {
        if (!m_open)
            return false;
        m_open = false;
        if (m_savepoint) {
            QSqlQuery release(m_database);
            if (release.exec(QStringLiteral("RELEASE linuxbee_edit")))
                return true;
            fail(release.lastError().text());
            QSqlQuery undo(m_database);
            undo.exec(QStringLiteral("ROLLBACK TO linuxbee_edit"));
            undo.exec(QStringLiteral("RELEASE linuxbee_edit"));
            return false;
        }
        if (m_database.commit())
            return true;
        fail(m_database.lastError().text());
        m_database.rollback();
        return false;
    }

    void rollBack()
    {
        if (!m_open)
            return;
        m_open = false;
        if (m_savepoint) {
            QSqlQuery undo(m_database);
            undo.exec(QStringLiteral("ROLLBACK TO linuxbee_edit"));
            undo.exec(QStringLiteral("RELEASE linuxbee_edit"));
        } else {
            m_database.rollback();
        }
    }

private:
    QSqlDatabase m_database;
    bool m_savepoint;
    bool m_open = false;
    QString m_error;
};

} // namespace

// L-EDIT: the ids that are stored and not already in `category`, which is
// exactly the set L-EDIT counts and L-SIGNALS carries.
// L-MANY, L-FAILED: pieces of one transaction; a refusal changes nothing, returns nothing and
// leaves the database's words in lastError().
QList<qint64> Store::setCategory(const QList<qint64> &ids, const QString &category)
{
    d->lastError.clear();
    if (!d->opened || ids.isEmpty())
        return { };
    QSqlDatabase database = QSqlDatabase::database(d->connection);
    EditScope scope(database, d->inBatch);
    if (!scope.isOpen()) {
        d->refused(scope.error());
        return { };
    }

    QList<qint64> changed;
    for (qsizetype from = 0; from < ids.size(); from += kEditPiece) {
        const QList<qint64> piece = ids.mid(from, kEditPiece);
        const IdPlaceholders placeholders = idPlaceholders(piece);
        QSqlQuery select(database);
        select.prepare(QStringLiteral("SELECT id FROM tracks WHERE (%1) AND category <> :category "
                                      "AND deleted_secs < 0")
                .arg(placeholders.condition));
        select.bindValue(QStringLiteral(":category"), category);
        bindIds(&select, placeholders.names, piece);
        if (!select.exec()) {
            d->refused(select.lastError().text());
            return { };
        }
        QList<qint64> found;
        while (select.next())
            found.append(select.value(0).toLongLong());
        if (found.isEmpty())
            continue;

        QSqlQuery update(database);
        update.prepare(
            QStringLiteral("UPDATE tracks SET category = :category WHERE (%1) AND deleted_secs < 0")
                .arg(placeholders.condition));
        update.bindValue(QStringLiteral(":category"), category);
        bindIds(&update, placeholders.names, piece);
        if (!update.exec()) {
            d->refused(update.lastError().text());
            return { };
        }
        changed += found;
    }
    if (!scope.commit()) {
        d->refused(scope.error());
        return { };
    }
    return changed;
}

// L-EDIT remove: forgets the rows and changes no file.
// L-MANY, L-FAILED: as setCategory.
QList<qint64> Store::removeIds(const QList<qint64> &ids)
{
    d->lastError.clear();
    if (!d->opened || ids.isEmpty())
        return { };
    QSqlDatabase database = QSqlDatabase::database(d->connection);
    EditScope scope(database, d->inBatch);
    if (!scope.isOpen()) {
        d->refused(scope.error());
        return { };
    }

    QList<qint64> gone;
    for (qsizetype from = 0; from < ids.size(); from += kEditPiece) {
        const QList<qint64> piece = ids.mid(from, kEditPiece);
        const IdPlaceholders placeholders = idPlaceholders(piece);
        QSqlQuery select(database);
        select.prepare(QStringLiteral("SELECT id FROM tracks WHERE (%1) AND deleted_secs < 0")
                .arg(placeholders.condition));
        bindIds(&select, placeholders.names, piece);
        if (!select.exec()) {
            d->refused(select.lastError().text());
            return { };
        }
        QList<qint64> found;
        while (select.next())
            found.append(select.value(0).toLongLong());
        if (found.isEmpty())
            continue;

        QSqlQuery drop(database);
        drop.prepare(QStringLiteral("DELETE FROM tracks WHERE (%1) AND deleted_secs < 0")
                .arg(placeholders.condition));
        bindIds(&drop, placeholders.names, piece);
        if (!drop.exec()) {
            d->refused(drop.lastError().text());
            return { };
        }
        gone += found;
    }
    if (!scope.commit()) {
        d->refused(scope.error());
        return { };
    }
    return gone;
}

QString Store::lastError() const
{
    return d->lastError;
}

// L-DELETED: the live tracks among `ids` are marked deleted at `whenSecs` and missing. As
// setCategory: pieces of one transaction, a refusal changes nothing and leaves its words.
QList<qint64> Store::markDeleted(const QList<qint64> &ids, qint64 whenSecs)
{
    d->lastError.clear();
    if (!d->opened || ids.isEmpty() || whenSecs < 0)
        return { };
    QSqlDatabase database = QSqlDatabase::database(d->connection);
    EditScope scope(database, d->inBatch);
    if (!scope.isOpen()) {
        d->refused(scope.error());
        return { };
    }

    QList<qint64> marked;
    for (qsizetype from = 0; from < ids.size(); from += kEditPiece) {
        const QList<qint64> piece = ids.mid(from, kEditPiece);
        const IdPlaceholders placeholders = idPlaceholders(piece);
        QSqlQuery select(database);
        select.prepare(QStringLiteral("SELECT id FROM tracks WHERE (%1) AND deleted_secs < 0")
                .arg(placeholders.condition));
        bindIds(&select, placeholders.names, piece);
        if (!select.exec()) {
            d->refused(select.lastError().text());
            return { };
        }
        QList<qint64> found;
        while (select.next())
            found.append(select.value(0).toLongLong());
        if (found.isEmpty())
            continue;

        QSqlQuery update(database);
        update.prepare(QStringLiteral("UPDATE tracks SET deleted_secs = :when, missing = 1 "
                                      "WHERE (%1) AND deleted_secs < 0")
                .arg(placeholders.condition));
        update.bindValue(QStringLiteral(":when"), whenSecs);
        bindIds(&update, placeholders.names, piece);
        if (!update.exec()) {
            d->refused(update.lastError().text());
            return { };
        }
        marked += found;
    }
    if (!scope.commit()) {
        d->refused(scope.error());
        return { };
    }
    return marked;
}

// L-DELETED: the deleted rows old enough to be looked at for removal.
LibraryRead<QList<std::pair<qint64, QString>>> Store::readDeletedUntil(qint64 cutoffSecs) const
{
    using Read = LibraryRead<QList<std::pair<qint64, QString>>>;
    if (!d->opened)
        return Read::failure(QStringLiteral("the library is not open"));
    QSqlQuery query(QSqlDatabase::database(d->connection));
    query.prepare(QStringLiteral("SELECT id, path FROM tracks WHERE deleted_secs >= 0 "
                                 "AND deleted_secs <= :cutoff ORDER BY id"));
    query.bindValue(QStringLiteral(":cutoff"), cutoffSecs);
    if (!query.exec())
        return Read::failure(readWords(query));
    QList<std::pair<qint64, QString>> rows;
    while (query.next())
        rows.append({ query.value(0).toLongLong(), query.value(1).toString() });
    if (endedInError(query))
        return Read::failure(readWords(query));
    return Read::ok(std::move(rows));
}

// L-DELETED: as removeIds, for deleted rows only.
QList<qint64> Store::removeDeleted(const QList<qint64> &ids)
{
    d->lastError.clear();
    if (!d->opened || ids.isEmpty())
        return { };
    QSqlDatabase database = QSqlDatabase::database(d->connection);
    EditScope scope(database, d->inBatch);
    if (!scope.isOpen()) {
        d->refused(scope.error());
        return { };
    }

    QList<qint64> gone;
    for (qsizetype from = 0; from < ids.size(); from += kEditPiece) {
        const QList<qint64> piece = ids.mid(from, kEditPiece);
        const IdPlaceholders placeholders = idPlaceholders(piece);
        QSqlQuery select(database);
        select.prepare(QStringLiteral("SELECT id FROM tracks WHERE (%1) AND deleted_secs >= 0")
                .arg(placeholders.condition));
        bindIds(&select, placeholders.names, piece);
        if (!select.exec()) {
            d->refused(select.lastError().text());
            return { };
        }
        QList<qint64> found;
        while (select.next())
            found.append(select.value(0).toLongLong());
        if (found.isEmpty())
            continue;

        QSqlQuery drop(database);
        drop.prepare(QStringLiteral("DELETE FROM tracks WHERE (%1) AND deleted_secs >= 0")
                .arg(placeholders.condition));
        bindIds(&drop, placeholders.names, piece);
        if (!drop.exec()) {
            d->refused(drop.lastError().text());
            return { };
        }
        gone += found;
    }
    if (!scope.commit()) {
        d->refused(scope.error());
        return { };
    }
    return gone;
}

LibraryRead<qint64> Store::readDeletedIdAt(const QString &path) const
{
    using Read = LibraryRead<qint64>;
    if (!d->opened)
        return Read::failure(QStringLiteral("the library is not open"));
    QSqlQuery query(QSqlDatabase::database(d->connection));
    query.prepare(QStringLiteral("SELECT id FROM tracks WHERE path = :path AND deleted_secs >= 0"));
    query.bindValue(QStringLiteral(":path"), path);
    if (!query.exec())
        return Read::failure(readWords(query));
    if (!query.next()) {
        if (endedInError(query))
            return Read::failure(readWords(query));
        return Read::ok(0);
    }
    return Read::ok(query.value(0).toLongLong());
}

// L-DELETED: the file is back at the deleted track's path, and is read as updateReading reads it.
// Everything only the library knows (id, section, date added, statistics) stays.
bool Store::restoreDeleted(qint64 id, const LibraryTrack &track)
{
    d->lastError.clear();
    if (!d->opened)
        return false;
    QSqlQuery query(QSqlDatabase::database(d->connection));
    query.prepare(QStringLiteral("UPDATE tracks SET deleted_secs = -1, missing = 0, "
                                 "size_bytes = :size, modified_secs = :modified, "
                                 "picture_count = :pictures, tags_json = :tags, reader = :reader "
                                 "WHERE id = :id AND deleted_secs >= 0"));
    query.bindValue(QStringLiteral(":size"), track.sizeBytes);
    query.bindValue(QStringLiteral(":modified"), toSecs(track.modified));
    query.bindValue(QStringLiteral(":pictures"), track.pictureCount);
    query.bindValue(QStringLiteral(":tags"), tagsToJson(track.tags));
    query.bindValue(QStringLiteral(":reader"), kReaderVersion);
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec()) {
        d->refused(query.lastError().text());
        return false;
    }
    return query.numRowsAffected() == 1;
}

// L-FAILED: false with lastError() empty means no such track; with words, the database refused.
bool Store::updateReading(qint64 id, const LibraryTrack &track)
{
    d->lastError.clear();
    if (!d->opened)
        return false;
    QSqlQuery query(QSqlDatabase::database(d->connection));
    query.prepare(QStringLiteral("UPDATE tracks SET size_bytes = :size, modified_secs = :modified, "
                                 "picture_count = :pictures, tags_json = :tags, reader = :reader "
                                 "WHERE id = :id AND deleted_secs < 0"));
    query.bindValue(QStringLiteral(":size"), track.sizeBytes);
    query.bindValue(QStringLiteral(":modified"), toSecs(track.modified));
    query.bindValue(QStringLiteral(":pictures"), track.pictureCount);
    query.bindValue(QStringLiteral(":tags"), tagsToJson(track.tags));
    query.bindValue(QStringLiteral(":reader"), kReaderVersion);
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec()) {
        d->refused(query.lastError().text());
        return false;
    }
    return query.numRowsAffected() == 1;
}

// L-DELETED: the path of a deleted track is free. When a deleted row holds the new path it is
// removed in the same change (one transaction, or a savepoint inside a batch), and only for a track
// that is live: a call that changes nothing removes nothing.
bool Store::setPath(qint64 id, const QString &newPath, bool missing)
{
    d->lastError.clear();
    if (!d->opened) {
        d->refused(QString());
        return false;
    }
    QSqlDatabase database = QSqlDatabase::database(d->connection);
    bool freeing = false;
    {
        QSqlQuery look(database);
        look.prepare(
            QStringLiteral("SELECT 1 FROM tracks WHERE path = :path AND deleted_secs >= 0"));
        look.bindValue(QStringLiteral(":path"), newPath);
        freeing = look.exec() && look.next();
    }
    std::optional<EditScope> scope;
    if (freeing) {
        scope.emplace(database, d->inBatch);
        if (!scope->isOpen()) {
            d->refused(scope->error());
            return false;
        }
        QSqlQuery drop(database);
        drop.prepare(QStringLiteral("DELETE FROM tracks WHERE path = :path AND deleted_secs >= 0 "
                                    "AND EXISTS (SELECT 1 FROM tracks WHERE id = :id "
                                    "AND deleted_secs < 0)"));
        drop.bindValue(QStringLiteral(":path"), newPath);
        drop.bindValue(QStringLiteral(":id"), id);
        if (!drop.exec()) {
            d->refused(drop.lastError().text());
            return false;
        }
    }
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "UPDATE tracks SET path = :path, missing = :missing WHERE id = :id AND deleted_secs < 0"));
    query.bindValue(QStringLiteral(":path"), newPath);
    query.bindValue(QStringLiteral(":missing"), missing);
    query.bindValue(QStringLiteral(":id"), id);
    if (query.exec() && query.numRowsAffected() == 1) {
        if (scope && !scope->commit()) {
            d->refused(scope->error());
            return false;
        }
        return true;
    }
    d->refused(query.lastError().text());
    return false;
}

// L-FAILED: false with lastError() empty means no such track (or an invalid time); with words,
// the database refused.
bool Store::recordPlay(qint64 id, const QDateTime &when)
{
    d->lastError.clear();
    if (!d->opened || !when.isValid())
        return false;
    QSqlQuery query(QSqlDatabase::database(d->connection));
    // L-STATS: the count always goes up, and "last played" moves forward only,
    // so an earlier play counts without pulling the date back. -1 in the column
    // means never played, which is earlier than any time.
    // The same instant is bound under two names on purpose: Qt's SQLite driver
    // binds each placeholder name once, so one name written twice in the text
    // would leave the second occurrence unbound.
    query.prepare(QStringLiteral("UPDATE tracks SET play_count = play_count + 1, "
                                 "last_played_secs = CASE WHEN last_played_secs = -1 "
                                 "OR last_played_secs < :whenFrom THEN :whenTo "
                                 "ELSE last_played_secs END WHERE id = :id AND deleted_secs < 0"));
    query.bindValue(QStringLiteral(":whenFrom"), when.toSecsSinceEpoch());
    query.bindValue(QStringLiteral(":whenTo"), when.toSecsSinceEpoch());
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec()) {
        d->refused(query.lastError().text());
        return false;
    }
    return query.numRowsAffected() == 1;
}

// L-FAILED: as recordPlay.
bool Store::recordSkip(qint64 id)
{
    d->lastError.clear();
    if (!d->opened)
        return false;
    QSqlQuery query(QSqlDatabase::database(d->connection));
    query.prepare(QStringLiteral("UPDATE tracks SET skip_count = skip_count + 1 "
                                 "WHERE id = :id AND deleted_secs < 0"));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec()) {
        d->refused(query.lastError().text());
        return false;
    }
    return query.numRowsAffected() == 1;
}

// L-FAILED: as recordPlay.
bool Store::setStatistics(qint64 id, const TrackStatistics &statistics)
{
    d->lastError.clear();
    if (!d->opened)
        return false;
    QSqlQuery query(QSqlDatabase::database(d->connection));
    query.prepare(QStringLiteral("UPDATE tracks SET date_added_secs = :added, "
                                 "play_count = :plays, skip_count = :skips, "
                                 "last_played_secs = :last WHERE id = :id AND deleted_secs < 0"));
    query.bindValue(QStringLiteral(":added"), toSecs(statistics.dateAdded));
    query.bindValue(QStringLiteral(":plays"), statistics.playCount);
    query.bindValue(QStringLiteral(":skips"), statistics.skipCount);
    query.bindValue(QStringLiteral(":last"), toSecs(statistics.lastPlayed));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec()) {
        d->refused(query.lastError().text());
        return false;
    }
    return query.numRowsAffected() == 1;
}

// L-STATS mergeStatistics. The comparing is done by the database in the statement, on the row as
// it is then. A time that is not valid is -1, which is earlier than any time held (as in
// recordPlay) and which gives no date added. Qt's SQLite driver binds each placeholder name once,
// so each use has a name of its own. An update that would change nothing matches no row.
// L-FAILED: as recordPlay.
bool Store::mergeStatistics(qint64 id, const TrackStatistics &statistics)
{
    d->lastError.clear();
    if (!d->opened)
        return false;
    QSqlQuery query(QSqlDatabase::database(d->connection));
    query.prepare(QStringLiteral(
        "UPDATE tracks SET "
        "date_added_secs = CASE WHEN :added1 <> -1 THEN :added2 ELSE date_added_secs END, "
        "play_count = max(play_count, :plays1), "
        "skip_count = max(skip_count, :skips1), "
        "last_played_secs = CASE WHEN :last1 <> -1 AND :last2 > last_played_secs "
        "THEN :last3 ELSE last_played_secs END "
        "WHERE id = :id AND deleted_secs < 0 AND ((:added3 <> -1 AND :added4 <> date_added_secs) "
        "OR :plays2 > play_count OR :skips2 > skip_count "
        "OR (:last4 <> -1 AND :last5 > last_played_secs))"));
    const qint64 added = toSecs(statistics.dateAdded);
    const qint64 last = toSecs(statistics.lastPlayed);
    for (const char *name : { ":added1", ":added2", ":added3", ":added4" })
        query.bindValue(QLatin1String(name), added);
    for (const char *name : { ":last1", ":last2", ":last3", ":last4", ":last5" })
        query.bindValue(QLatin1String(name), last);
    for (const char *name : { ":plays1", ":plays2" })
        query.bindValue(QLatin1String(name), statistics.playCount);
    for (const char *name : { ":skips1", ":skips2" })
        query.bindValue(QLatin1String(name), statistics.skipCount);
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec()) {
        d->refused(query.lastError().text());
        return false;
    }
    return query.numRowsAffected() == 1;
}

// L-SCAN, L-MISSING: the file is gone, or is back. Only the flag moves: tags,
// statistics and id stay, which is what a missing track keeps.
// L-FAILED: as updateReading.
bool Store::setMissing(qint64 id, bool missing)
{
    d->lastError.clear();
    if (!d->opened)
        return false;
    QSqlQuery query(QSqlDatabase::database(d->connection));
    query.prepare(
        QStringLiteral("UPDATE tracks SET missing = :missing WHERE id = :id AND deleted_secs < 0"));
    query.bindValue(QStringLiteral(":missing"), missing);
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec()) {
        d->refused(query.lastError().text());
        return false;
    }
    return query.numRowsAffected() == 1;
}

} // namespace linuxbee::library
