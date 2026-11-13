#include "formatdetail.h"

#include <QMap>

namespace linuxbee::playlists {

namespace {

struct PlsRow {
    QString location; // raw, as written; resolved when the entry is built
    QString title;
    int seconds = -1;
};

// Y-READ: a key is letters then the entry number, keys in any case. N of 1 or
// more describes entry N; anything else is no concern here.
bool entryKey(const QString &key, QString *kind, int *number)
{
    int letters = 0;
    while (letters < key.size() && key.at(letters).isLetter())
        ++letters;
    if (letters == 0 || letters == key.size())
        return false;
    bool ok = false;
    const int n = key.mid(letters).toInt(&ok);
    if (!ok || n < 1)
        return false;
    *kind = key.left(letters);
    *number = n;
    return true;
}

}

QList<PlaylistEntry> readPls(const QByteArray &data, const QString &baseFolder)
{
    QMap<int, PlsRow> rows; // QMap is in ascending order of the key
    for (const QString &line : decodeLines(data, PlaylistFormat::Pls)) {
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals < 0)
            continue; // Y-READ: every line that is not Key=Value is ignored
        QString kind;
        int number = 0;
        if (!entryKey(line.left(equals).trimmed().toLower(), &kind, &number))
            continue;
        const QString value = line.mid(equals + 1).trimmed();
        PlsRow &row = rows[number];
        if (kind == QLatin1String("file"))
            row.location = value;
        else if (kind == QLatin1String("title"))
            row.title = value;
        else if (kind == QLatin1String("length")) {
            bool ok = false;
            const int seconds = value.toInt(&ok);
            // Y-READ: a length that is negative or not a whole number is -1.
            row.seconds = (ok && seconds >= 0) ? seconds : -1;
        }
    }

    QList<PlaylistEntry> entries;
    for (const PlsRow &row : std::as_const(rows)) {
        if (row.location.isEmpty())
            continue; // Y-READ: an entry with no FileN is dropped
        entries.append(
            PlaylistEntry { resolveLocation(row.location, baseFolder), row.title, row.seconds });
    }
    return entries;
}

QByteArray writePls(const QList<PlaylistEntry> &entries, const PlaylistWriteOptions &options)
{
    // Y-WRITE: UTF-8 with "\n" line ends.
    QByteArray data = QByteArrayLiteral("[playlist]\n");
    int number = 0;
    for (const PlaylistEntry &entry : entries) {
        ++number;
        const QByteArray n = QByteArray::number(number);
        data += QByteArrayLiteral("File") + n + '=' + writtenLocation(entry.path, options).toUtf8()
            + '\n';
        if (!entry.title.isEmpty())
            data += QByteArrayLiteral("Title") + n + '=' + entry.title.toUtf8() + '\n';
        data += QByteArrayLiteral("Length") + n + '=' + QByteArray::number(entry.durationSeconds)
            + '\n';
    }
    data += QByteArrayLiteral("NumberOfEntries=") + QByteArray::number(int(entries.size()))
        + QByteArrayLiteral("\nVersion=2\n");
    return data;
}

}
