#include "changes.h"

#include "catalogue.h"
#include "tagreader.h"

namespace {

// W-FIELDS: values that are empty or only white space are dropped.
QStringList cleaned(const QStringList &values)
{
    QStringList kept;
    for (const QString &value : values) {
        const QString trimmed = value.trimmed();
        if (!trimmed.isEmpty())
            kept.append(trimmed);
    }
    return kept;
}

// The catalogue name a caller wrote, matched ignoring case; empty when none.
QString canonicalName(const QString &name)
{
    for (const QString &field : linuxbee::tagFieldNames()) {
        if (field.compare(name, Qt::CaseInsensitive) == 0)
            return field;
    }
    return { };
}

// W-INVALID for a rating: one number, a whole or half star from 0.5 to 5.
// popmFromRating gives 0 for every other number, so it is the test.
bool isRatingValue(const QString &text)
{
    bool parsed = false;
    const double stars = text.toDouble(&parsed);
    return parsed && linuxbee::popmFromRating(stars) != 0;
}

// W-INVALID for Love: one letter, L or B, in either case.
bool isLoveLetter(const QString &text)
{
    const QString letter = text.trimmed().toUpper();
    return letter == QLatin1String("L") || letter == QLatin1String("B");
}

// Empty removes the field and is always allowed. Returns "" when the values are fine.
QString fieldProblem(const QString &field, const QStringList &values)
{
    if (values.isEmpty())
        return { };
    if (field == QLatin1String("Rating")) {
        if (values.size() == 1 && isRatingValue(values.first()))
            return { };
        return QStringLiteral("a rating is one number from 0.5 to 5 in steps of 0.5");
    }
    if (field == QLatin1String("Love")) {
        if (values.size() == 1 && isLoveLetter(values.first()))
            return { };
        return QStringLiteral("a Love is one letter, L or B");
    }
    const bool singleNumber = field == QLatin1String("Track#")
        || field == QLatin1String("Track Count") || field == QLatin1String("Disc#")
        || field == QLatin1String("Disc Count");
    if (singleNumber) {
        if (values.size() == 1 && !values.first().contains(QLatin1Char('/')))
            return { };
        return QStringLiteral("one number, without a /");
    }
    return { };
}

// W-INVALID for an other key. Returns "" when the key is usable.
QString otherKeyProblem(const QString &key)
{
    if (key.isEmpty())
        return QStringLiteral("the key is empty");
    for (const QChar c : key) {
        if (c.unicode() < 0x20 || c.unicode() > 0x7E)
            return QStringLiteral("the key has a character outside printable ASCII");
    }
    if (key.contains(QLatin1Char('=')))
        return QStringLiteral("the key contains =");
    const QString upper = key.toUpper();
    // WORK is read as Grouping in ID3v2, so it is refused in every format.
    if (upper == QLatin1String("METADATA_BLOCK_PICTURE") || upper == QLatin1String("WORK")
        || linuxbee::tags::isCatalogueKey(upper))
        return QStringLiteral("this key belongs to a field or to pictures");
    return { };
}

}

namespace linuxbee::tags {

CheckedChanges checkChanges(const TagChanges &changes)
{
    CheckedChanges out;

    for (auto it = changes.fields.cbegin(); it != changes.fields.cend(); ++it) {
        const QString field = canonicalName(it.key());
        if (field.isEmpty()) {
            out.problems.append(it.key() + QStringLiteral(": not a field in the catalogue"));
            continue;
        }
        const QStringList values = cleaned(it.value());
        const QString problem = fieldProblem(field, values);
        if (!problem.isEmpty()) {
            out.problems.append(it.key() + QStringLiteral(": ") + problem);
            continue;
        }
        // W-INVALID: Love is stored in upper case.
        if (field == QLatin1String("Love") && !values.isEmpty())
            out.fields.insert(field, { values.first().toUpper() });
        else
            out.fields.insert(field, values);
    }

    for (auto it = changes.other.cbegin(); it != changes.other.cend(); ++it) {
        const QString problem = otherKeyProblem(it.key());
        if (!problem.isEmpty()) {
            out.problems.append(it.key() + QStringLiteral(": ") + problem);
            continue;
        }
        out.other.insert(it.key().toUpper(), cleaned(it.value()));
    }

    // W-INVALID: a picture needs data. Its type and MIME type may be empty (W-PICTURES).
    if (changes.pictures.has_value()) {
        const QList<Picture> &pictures = *changes.pictures;
        for (qsizetype i = 0; i < pictures.size(); ++i) {
            if (pictures.at(i).data.isEmpty()) {
                out.problems.append(
                    QStringLiteral("picture %1: the picture has no data").arg(i + 1));
            }
        }
    }

    return out;
}

TrackTags applyTo(const TrackTags &now, const CheckedChanges &changes,
    const std::optional<QList<Picture>> &pictures)
{
    TrackTags after = now;
    for (auto it = changes.fields.cbegin(); it != changes.fields.cend(); ++it) {
        if (it.value().isEmpty())
            after.fields.remove(it.key());
        else
            after.fields.insert(it.key(), it.value());
    }
    for (auto it = changes.other.cbegin(); it != changes.other.cend(); ++it) {
        if (it.value().isEmpty())
            after.other.remove(it.key());
        else
            after.other.insert(it.key(), it.value());
    }
    if (pictures.has_value()) {
        // W-PICTURES: an empty type is a front cover; an empty MIME type is sniffed.
        QList<Picture> normalized = *pictures;
        for (Picture &picture : normalized) {
            if (picture.type.isEmpty())
                picture.type = QStringLiteral("Front Cover");
            if (picture.mimeType.isEmpty())
                picture.mimeType = imageMimeType(picture.data);
        }
        after.pictures = normalized;
    }
    return after;
}

bool sameTags(const TrackTags &a, const TrackTags &b)
{
    if (a.fields != b.fields || a.other != b.other)
        return false;
    if (a.pictures.size() != b.pictures.size())
        return false;

    // W-UNCHANGED: MPEG-4 stores neither a picture type nor a description.
    const bool typed = a.format != QLatin1String("m4a");
    for (qsizetype i = 0; i < a.pictures.size(); ++i) {
        const Picture &x = a.pictures.at(i);
        const Picture &y = b.pictures.at(i);
        if (x.data != y.data || x.mimeType != y.mimeType)
            return false;
        if (typed && (x.type != y.type || x.description != y.description))
            return false;
    }
    return true;
}

}
