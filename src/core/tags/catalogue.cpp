#include "catalogue.h"

#include <QSet>

#include "tagreader.h"

namespace {

using linuxbee::TrackTags;

QStringList withoutBlanks(const QStringList &values)
{
    QStringList kept;
    for (const QString &value : values) {
        if (!value.trimmed().isEmpty())
            kept.append(value);
    }
    return kept;
}

// R-NUMBERS: "n/total" in the first value. An empty side sets nothing, and a
// total already set from its own key is kept.
void splitNumber(TrackTags &tags, const QString &field, const QString &totalField)
{
    if (!tags.fields.contains(field))
        return;
    QStringList values = tags.fields.value(field);
    const QString first = values.first();
    const qsizetype slash = first.indexOf(QLatin1Char('/'));
    if (slash < 0)
        return;
    const QString number = first.left(slash).trimmed();
    const QString total = first.mid(slash + 1).trimmed();

    values.removeFirst();
    if (!number.isEmpty())
        values.prepend(number);
    tags.fields.remove(field);
    if (!values.isEmpty())
        tags.fields.insert(field, values);

    if (!total.isEmpty() && !tags.fields.contains(totalField))
        tags.fields.insert(totalField, { total });
}

}

namespace linuxbee::tags {

const QList<FieldKeys> &fieldTable()
{
    static const QList<FieldKeys> table = {
        { QStringLiteral("Title"), { QStringLiteral("TITLE") } },
        { QStringLiteral("Artist"), { QStringLiteral("ARTIST") } },
        { QStringLiteral("Album"), { QStringLiteral("ALBUM") } },
        { QStringLiteral("Album Artist"),
            { QStringLiteral("ALBUMARTIST"), QStringLiteral("ALBUM ARTIST") } },
        { QStringLiteral("Year"), { QStringLiteral("DATE"), QStringLiteral("YEAR") } },
        { QStringLiteral("Track#"), { QStringLiteral("TRACKNUMBER") } },
        { QStringLiteral("Track Count"),
            { QStringLiteral("TRACKTOTAL"), QStringLiteral("TOTALTRACKS") } },
        { QStringLiteral("Disc#"), { QStringLiteral("DISCNUMBER") } },
        { QStringLiteral("Disc Count"),
            { QStringLiteral("DISCTOTAL"), QStringLiteral("TOTALDISCS") } },
        { QStringLiteral("Genre"), { QStringLiteral("GENRE") } },
        { QStringLiteral("Composer"), { QStringLiteral("COMPOSER") } },
        { QStringLiteral("Conductor"), { QStringLiteral("CONDUCTOR") } },
        { QStringLiteral("Publisher"),
            { QStringLiteral("LABEL"), QStringLiteral("ORGANIZATION"),
                QStringLiteral("PUBLISHER") } },
        { QStringLiteral("Grouping"),
            { QStringLiteral("GROUPING"), QStringLiteral("CONTENTGROUP") } },
        { QStringLiteral("Comment"), { QStringLiteral("COMMENT") } },
        { QStringLiteral("Rating"), { } },
        { QStringLiteral("BPM"), { QStringLiteral("BPM") } },
        { QStringLiteral("Mood"), { QStringLiteral("MOOD") } },
        { QStringLiteral("Lyricist"), { QStringLiteral("LYRICIST") } },
        { QStringLiteral("Lyrics"),
            { QStringLiteral("LYRICS"), QStringLiteral("UNSYNCEDLYRICS") } },
        { QStringLiteral("Compilation"), { QStringLiteral("COMPILATION") } },
        { QStringLiteral("Copyright"), { QStringLiteral("COPYRIGHT") } },
        { QStringLiteral("ISRC"), { QStringLiteral("ISRC") } },
        { QStringLiteral("Sort Title"), { QStringLiteral("TITLESORT") } },
        { QStringLiteral("Sort Artist"), { QStringLiteral("ARTISTSORT") } },
        { QStringLiteral("Sort Album"), { QStringLiteral("ALBUMSORT") } },
        { QStringLiteral("Sort Album Artist"), { QStringLiteral("ALBUMARTISTSORT") } },
        { QStringLiteral("Sort Composer"), { QStringLiteral("COMPOSERSORT") } },
        { QStringLiteral("Original Year"),
            { QStringLiteral("ORIGINALDATE"), QStringLiteral("ORIGYEAR") } },
        { QStringLiteral("Original Artist"),
            { QStringLiteral("ORIGINALARTIST"), QStringLiteral("ORIGARTIST") } },
        { QStringLiteral("Original Album"),
            { QStringLiteral("ORIGINALALBUM"), QStringLiteral("ORIGALBUM") } },
        { QStringLiteral("Subtitle"), { QStringLiteral("SUBTITLE") } },
        { QStringLiteral("Language"), { QStringLiteral("LANGUAGE") } },
        { QStringLiteral("Initial Key"), { QStringLiteral("INITIALKEY") } },
        { QStringLiteral("Encoded By"), { QStringLiteral("ENCODEDBY") } },
        { QStringLiteral("Mix Artist"), { QStringLiteral("REMIXER") } },
    };
    return table;
}

void assignFields(QMap<QString, QStringList> properties, bool id3, TrackTags &tags)
{
    // R-FIELDS: keys are compared in upper case.
    QMap<QString, QStringList> upper;
    for (auto it = properties.cbegin(); it != properties.cend(); ++it)
        upper[it.key().toUpper()].append(it.value());

    // The first key with a value wins; the keys that lose stay in `claimed` only if they won.
    QSet<QString> claimed;
    for (const FieldKeys &entry : fieldTable()) {
        QStringList keys = entry.keys;
        // R-GROUPING: in ID3v2 (MP3, WAV) TIT1 arrives as WORK, read before the others.
        if (id3 && entry.field == QLatin1String("Grouping"))
            keys.prepend(QStringLiteral("WORK"));
        for (const QString &key : keys) {
            const QStringList values = withoutBlanks(upper.value(key));
            if (values.isEmpty())
                continue;
            tags.fields.insert(entry.field, values);
            claimed.insert(key);
            break;
        }
    }

    // R-RATING, text half: the first RATING value read as a number. A value
    // that is not a rating stays in `other`.
    const QStringList ratingText = withoutBlanks(upper.value(QStringLiteral("RATING")));
    if (!ratingText.isEmpty()) {
        if (const std::optional<double> stars = ratingFromText(ratingText.first())) {
            tags.fields.insert(QStringLiteral("Rating"), { ratingToString(*stars) });
            claimed.insert(QStringLiteral("RATING"));
        }
    }

    // R-NUMBERS
    splitNumber(tags, QStringLiteral("Track#"), QStringLiteral("Track Count"));
    splitNumber(tags, QStringLiteral("Disc#"), QStringLiteral("Disc Count"));

    // R-OTHER: every text key no field claimed, including the losing key of a
    // field with two keys. METADATA_BLOCK_PICTURE is a picture, never text.
    for (auto it = upper.cbegin(); it != upper.cend(); ++it) {
        if (claimed.contains(it.key()) || it.key() == QLatin1String("METADATA_BLOCK_PICTURE"))
            continue;
        const QStringList values = withoutBlanks(it.value());
        if (!values.isEmpty())
            tags.other.insert(it.key(), values);
    }
}

}

namespace linuxbee {

QStringList tagFieldNames()
{
    QStringList names;
    for (const tags::FieldKeys &entry : tags::fieldTable())
        names.append(entry.field);
    return names;
}

}
