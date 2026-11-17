#include "store.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimeZone>
#include <QVariant>

#include <atomic>

namespace linuxbee::library {
namespace {

// L-SCHEMA: this version of the schema. A file with a lower number is brought
// up to date when opened; a higher one is refused.
constexpr int kSchemaVersionValue = 1;

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
    "skip_count, last_played_secs, missing, picture_count, tags_json)"
    " VALUES (:path, :category, :size, :modified, :added, :plays, :skips, :lastPlayed, :missing, "
    ":pictures, :tags)");

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

// L-SCHEMA: create what a version-0 file has no rows for. All of it is this
// program's own text: nothing here carries a path, a tag or any other data.
// L-WAIT, L-OPEN: the write lock is taken first and waited for. Another start may have made the
// tables while this one waited, so the version is looked at again once the lock is held; then
// there is nothing to make and this succeeds.
bool createSchema(QSqlDatabase &database, QString *error)
{
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("BEGIN IMMEDIATE"))) {
        setError(error,
            QStringLiteral("Cannot start writing a new library: %1").arg(query.lastError().text()));
        return false;
    }
    {
        QSqlQuery again(database);
        if (again.exec(QStringLiteral("PRAGMA user_version")) && again.next()
            && again.value(0).toInt() >= kSchemaVersionValue) {
            again.finish();
            database.rollback();
            return true;
        }
    }
    const QString statements[] = {
        QStringLiteral("CREATE TABLE tracks ("
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
                       "tags_json TEXT NOT NULL)"),
        QStringLiteral("CREATE INDEX tracks_by_category ON tracks (category)"),
        // The schema number lives in SQLite's user_version. It is this
        // program's own number, so it is written out and not bound.
        QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersionValue),
    };
    for (const QString &statement : statements) {
        if (!query.exec(statement)) {
            const QString reason = query.lastError().text();
            database.rollback();
            setError(error, QStringLiteral("Cannot create the library tables: %1").arg(reason));
            return false;
        }
    }
    if (!database.commit()) {
        const QString reason = database.lastError().text();
        database.rollback();
        setError(error, QStringLiteral("Cannot save the new library: %1").arg(reason));
        return false;
    }
    return true;
}

// Opens the file and checks what it holds. The QSqlDatabase for `connection`
// is gone when this returns, which is what lets the caller remove it.
bool openAndCheckSchema(const QString &databasePath, const QString &connection, QString *error)
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
    if (found < kSchemaVersionValue && !createSchema(database, error))
        return false;
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
    QString lastReadError; // L-WAIT: the database's words when the last byId or byPath failed

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

bool Store::open(const QString &databasePath, QString *error)
{
    const QString name = Private::nextName();
    if (!openAndCheckSchema(databasePath, name, error)) {
        QSqlDatabase::removeDatabase(name);
        return false;
    }
    d->connection = name;
    d->opened = true;
    return true;
}

int Store::count() const
{
    if (!d->opened)
        return 0;
    QSqlQuery query(QSqlDatabase::database(d->connection));
    if (!query.exec(QStringLiteral("SELECT count(*) FROM tracks")) || !query.next())
        return 0;
    return query.value(0).toInt();
}

QMap<QString, int> Store::categoryCounts() const
{
    QMap<QString, int> counts;
    if (!d->opened)
        return counts;
    QSqlQuery query(QSqlDatabase::database(d->connection));
    if (!query.exec(QStringLiteral("SELECT category, count(*) FROM tracks GROUP BY category")))
        return counts;
    while (query.next())
        counts.insert(query.value(0).toString(), query.value(1).toInt());
    return counts;
}

std::optional<LibraryTrack> Store::byId(qint64 id) const
{
    d->lastReadError.clear();
    if (!d->opened)
        return std::nullopt;
    QSqlQuery query(QSqlDatabase::database(d->connection));
    query.prepare(QStringLiteral("%1 WHERE id = :id").arg(QLatin1String(kSelectTrack)));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec()) {
        d->lastReadError = readWords(query);
        return std::nullopt;
    }
    if (!query.next())
        return std::nullopt;
    return rowFromRead(query);
}

ScanKnown Store::scanKnown() const
{
    ScanKnown known;
    if (!d->opened)
        return known;
    QSqlQuery query(QSqlDatabase::database(d->connection));
    if (!query.exec(
            QStringLiteral("SELECT id, path, size_bytes, modified_secs, missing FROM tracks")))
        return known;
    while (query.next()) {
        ScanKnown::Entry entry;
        entry.id = query.value(0).toLongLong();
        entry.sizeBytes = query.value(2).toLongLong();
        entry.modified = toDateTime(query.value(3).toLongLong());
        entry.missing = query.value(4).toBool();
        known.byPath.insert(query.value(1).toString(), entry);
    }
    return known;
}

QHash<qint64, ScanFindings::Judged> Store::heldById(const QList<qint64> &ids) const
{
    QHash<qint64, ScanFindings::Judged> held;
    if (!d->opened)
        return held;
    // SQLite refuses an expression tree deeper than 1000, and the ids are an
    // OR chain, so a long list is asked for in pieces.
    constexpr qsizetype kPiece = 400;
    for (qsizetype from = 0; from < ids.size(); from += kPiece) {
        const QList<qint64> piece = ids.mid(from, kPiece);
        const IdPlaceholders placeholders = idPlaceholders(piece);
        QSqlQuery query(QSqlDatabase::database(d->connection));
        query.prepare(QStringLiteral("SELECT id, path, size_bytes, modified_secs, missing "
                                     "FROM tracks WHERE (%1)")
                .arg(placeholders.condition));
        bindIds(&query, placeholders.names, piece);
        if (!query.exec())
            continue;
        while (query.next()) {
            ScanFindings::Judged entry;
            entry.path = query.value(1).toString();
            entry.sizeBytes = query.value(2).toLongLong();
            entry.modified = toDateTime(query.value(3).toLongLong());
            entry.missing = query.value(4).toBool();
            held.insert(query.value(0).toLongLong(), entry);
        }
    }
    return held;
}

std::optional<LibraryTrack> Store::byPath(const QString &path) const
{
    d->lastReadError.clear();
    if (!d->opened)
        return std::nullopt;
    QSqlQuery query(QSqlDatabase::database(d->connection));
    query.prepare(QStringLiteral("%1 WHERE path = :path").arg(QLatin1String(kSelectTrack)));
    query.bindValue(QStringLiteral(":path"), path);
    if (!query.exec()) {
        d->lastReadError = readWords(query);
        return std::nullopt;
    }
    if (!query.next())
        return std::nullopt;
    return rowFromRead(query);
}

QList<LibraryTrack> Store::all(const QString &category) const
{
    QList<LibraryTrack> list;
    if (!d->opened)
        return list;
    QSqlQuery query(QSqlDatabase::database(d->connection));
    if (category.isEmpty()) {
        query.prepare(QStringLiteral("%1 ORDER BY path").arg(QLatin1String(kSelectTrack)));
    } else {
        query.prepare(QStringLiteral("%1 WHERE category = :category ORDER BY path")
                .arg(QLatin1String(kSelectTrack)));
        query.bindValue(QStringLiteral(":category"), category);
    }
    if (!query.exec())
        return list;
    while (query.next())
        list.append(rowFromRead(query));
    return list;
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
    if (!query.exec()) {
        d->refused(query.lastError().text());
        return -1;
    }
    return query.lastInsertId().toLongLong();
}

namespace {

// L-MANY: a long list of ids is asked for in pieces (the same size heldById uses), because the
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
        select.prepare(QStringLiteral("SELECT id FROM tracks WHERE (%1) AND category <> :category")
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
        update.prepare(QStringLiteral("UPDATE tracks SET category = :category WHERE (%1)")
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
        select.prepare(
            QStringLiteral("SELECT id FROM tracks WHERE (%1)").arg(placeholders.condition));
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
        drop.prepare(QStringLiteral("DELETE FROM tracks WHERE (%1)").arg(placeholders.condition));
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

QString Store::lastReadError() const
{
    return d->lastReadError;
}

// L-FAILED: false with lastError() empty means no such track; with words, the database refused.
bool Store::updateReading(qint64 id, const LibraryTrack &track)
{
    d->lastError.clear();
    if (!d->opened)
        return false;
    QSqlQuery query(QSqlDatabase::database(d->connection));
    query.prepare(QStringLiteral("UPDATE tracks SET size_bytes = :size, modified_secs = :modified, "
                                 "picture_count = :pictures, tags_json = :tags WHERE id = :id"));
    query.bindValue(QStringLiteral(":size"), track.sizeBytes);
    query.bindValue(QStringLiteral(":modified"), toSecs(track.modified));
    query.bindValue(QStringLiteral(":pictures"), track.pictureCount);
    query.bindValue(QStringLiteral(":tags"), tagsToJson(track.tags));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec()) {
        d->refused(query.lastError().text());
        return false;
    }
    return query.numRowsAffected() == 1;
}

bool Store::setPath(qint64 id, const QString &newPath, bool missing)
{
    d->lastError.clear();
    if (!d->opened) {
        d->refused(QString());
        return false;
    }
    QSqlQuery query(QSqlDatabase::database(d->connection));
    query.prepare(
        QStringLiteral("UPDATE tracks SET path = :path, missing = :missing WHERE id = :id"));
    query.bindValue(QStringLiteral(":path"), newPath);
    query.bindValue(QStringLiteral(":missing"), missing);
    query.bindValue(QStringLiteral(":id"), id);
    if (query.exec() && query.numRowsAffected() == 1)
        return true;
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
                                 "ELSE last_played_secs END WHERE id = :id"));
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
    query.prepare(QStringLiteral("UPDATE tracks SET skip_count = skip_count + 1 WHERE id = :id"));
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
                                 "last_played_secs = :last WHERE id = :id"));
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

// L-SCAN, L-MISSING: the file is gone, or is back. Only the flag moves: tags,
// statistics and id stay, which is what a missing track keeps.
// L-FAILED: as updateReading.
bool Store::setMissing(qint64 id, bool missing)
{
    d->lastError.clear();
    if (!d->opened)
        return false;
    QSqlQuery query(QSqlDatabase::database(d->connection));
    query.prepare(QStringLiteral("UPDATE tracks SET missing = :missing WHERE id = :id"));
    query.bindValue(QStringLiteral(":missing"), missing);
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec()) {
        d->refused(query.lastError().text());
        return false;
    }
    return query.numRowsAffected() == 1;
}

} // namespace linuxbee::library
