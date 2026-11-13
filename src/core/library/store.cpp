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

QDateTime toDateTime(qint64 secs)
{
    if (secs == kNoTime)
        return QDateTime();
    return QDateTime::fromSecsSinceEpoch(secs, QTimeZone::UTC);
}

QStringList toStringList(const QJsonArray &array)
{
    QStringList values;
    for (const QJsonValue &value : array)
        values.append(value.toString());
    return values;
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

// One track, every column, in the order rowFromRead reads them. A statement,
// not data: no value is ever put into SQL text.
const QLatin1String kSelectTrack(
    "SELECT id, path, category, size_bytes, modified_secs, date_added_secs, play_count, "
    "skip_count, last_played_secs, missing, picture_count, tags_json FROM tracks");

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
bool createSchema(QSqlDatabase &database, QString *error)
{
    if (!database.transaction()) {
        setError(error,
            QStringLiteral("Cannot start writing a new library: %1")
                .arg(database.lastError().text()));
        return false;
    }
    QSqlQuery query(database);
    const QString statements[] = {
        QStringLiteral("CREATE TABLE tracks ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "path TEXT NOT NULL UNIQUE,"
                       "category TEXT NOT NULL,"
                       "size_bytes INTEGER NOT NULL,"
                       "modified_secs INTEGER NOT NULL,"
                       "date_added_secs INTEGER NOT NULL,"
                       "play_count INTEGER NOT NULL DEFAULT 0,"
                       "skip_count INTEGER NOT NULL DEFAULT 0,"
                       "last_played_secs INTEGER NOT NULL DEFAULT (-1),"
                       "missing INTEGER NOT NULL DEFAULT 0,"
                       "picture_count INTEGER NOT NULL DEFAULT 0,"
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
        setError(error,
            QStringLiteral("That file is not a linuxBee library: %1")
                .arg(version.lastError().text()));
        return false;
    }
    const int found = version.value(0).toInt();
    if (found > kSchemaVersionValue) {
        setError(error,
            QStringLiteral("That library was written by a newer linuxBee: schema %1, "
                           "this linuxBee reads schema %2")
                .arg(found)
                .arg(kSchemaVersionValue));
        return false;
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

std::optional<LibraryTrack> Store::byId(qint64 id) const
{
    if (!d->opened)
        return std::nullopt;
    QSqlQuery query(QSqlDatabase::database(d->connection));
    query.prepare(QStringLiteral("%1 WHERE id = :id").arg(QLatin1String(kSelectTrack)));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec() || !query.next())
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

} // namespace linuxbee::library
