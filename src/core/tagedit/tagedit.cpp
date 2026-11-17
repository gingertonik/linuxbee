#include "tagedit/tagedit.h"

#include <QRegularExpression>

#include <cmath>

namespace linuxbee {

namespace {

// The catalogue spelling of a field name, matched without regard to case;
// empty when the name is not a catalogue field.
QString catalogueName(const QString &fieldName)
{
    for (const QString &name : tagFieldNames()) {
        if (name.compare(fieldName, Qt::CaseInsensitive) == 0)
            return name;
    }
    return { };
}

}

QStringList tagEditorFields()
{
    return {
        QStringLiteral("Title"),
        QStringLiteral("Artist"),
        QStringLiteral("Album Artist"),
        QStringLiteral("Album"),
        QStringLiteral("Year"),
        QStringLiteral("Track#"),
        QStringLiteral("Track Count"),
        QStringLiteral("Disc#"),
        QStringLiteral("Disc Count"),
        QStringLiteral("Genre"),
        QStringLiteral("Composer"),
        QStringLiteral("Grouping"),
        QStringLiteral("Publisher"),
        QStringLiteral("BPM"),
        QStringLiteral("Rating"),
        QStringLiteral("Love"),
        QStringLiteral("Comment"),
    };
}

bool isListField(const QString &fieldName)
{
    static const QStringList listFields = {
        QStringLiteral("Artist"),
        QStringLiteral("Genre"),
        QStringLiteral("Composer"),
        QStringLiteral("Conductor"),
        QStringLiteral("Lyricist"),
        QStringLiteral("Mood"),
        QStringLiteral("Mix Artist"),
        QStringLiteral("Original Artist"),
    };
    for (const QString &name : listFields) {
        if (name.compare(fieldName, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

FieldState fieldState(const QList<TrackTags> &tracks, const QString &fieldName)
{
    FieldState st;
    const QString name = catalogueName(fieldName);
    if (name.isEmpty() || tracks.isEmpty())
        return st;

    int withValue = 0;
    for (const TrackTags &track : tracks) {
        if (!track.fields.value(name).isEmpty())
            ++withValue;
    }
    if (withValue == 0)
        return st; // Blank
    if (withValue < tracks.size()) {
        st.kind = FieldState::Kind::Mixed;
        return st;
    }

    const QStringList first = tracks.first().fields.value(name);
    for (const TrackTags &track : tracks) {
        if (track.fields.value(name) != first) {
            st.kind = FieldState::Kind::Mixed;
            return st;
        }
    }
    st.kind = FieldState::Kind::Same;
    st.text = first.join(QStringLiteral("; "));
    return st;
}

QStringList valuesFromText(const QString &fieldName, const QString &text)
{
    QStringList values;
    if (isListField(fieldName)) {
        for (const QString &part : text.split(QLatin1Char(';'))) {
            const QString value = part.trimmed();
            if (!value.isEmpty() && !values.contains(value))
                values.append(value);
        }
        return values;
    }
    const QString value = text.trimmed();
    if (!value.isEmpty())
        values.append(value);
    return values;
}

namespace {

// E-CHECK: digits only, leading zeros allowed, 1 to 9999.
bool isWholeNumberFrom1To9999(const QString &value)
{
    static const QRegularExpression digitsOnly(QStringLiteral("^[0-9]+$"));
    if (!digitsOnly.match(value).hasMatch())
        return false;
    QString significant = value;
    while (significant.startsWith(QLatin1Char('0')))
        significant.remove(0, 1);
    if (significant.isEmpty() || significant.size() > 4)
        return false;
    bool ok = false;
    const int number = significant.toInt(&ok);
    return ok && number >= 1 && number <= 9999;
}

// E-CHECK: 0.5 to 5 in steps of 0.5, written as a plain decimal.
bool isRatingStep(const QString &value)
{
    static const QRegularExpression decimal(QStringLiteral("^[0-9]+(\\.[0-9]+)?$"));
    if (!decimal.match(value).hasMatch())
        return false;
    bool ok = false;
    const double stars = value.toDouble(&ok);
    if (!ok || stars < 0.5 || stars > 5.0)
        return false;
    const double halves = stars * 2.0;
    return halves == std::floor(halves);
}

}

QString editProblem(const QString &fieldName, const QString &text)
{
    const QString name = catalogueName(fieldName);
    if (name.isEmpty())
        return fieldName + QStringLiteral(" is not a tag field.");

    const QString value = text.trimmed();
    if (value.isEmpty())
        return { };

    if (name == QLatin1String("Track#") || name == QLatin1String("Track Count")
        || name == QLatin1String("Disc#") || name == QLatin1String("Disc Count")) {
        if (!isWholeNumberFrom1To9999(value))
            return name + QStringLiteral(" must be a whole number from 1 to 9999.");
    }
    if (name == QLatin1String("Rating") && !isRatingStep(value))
        return QStringLiteral("Rating must be from 0.5 to 5 in steps of 0.5.");
    if (name == QLatin1String("Love")) {
        const QString letter = value.toUpper();
        if (letter != QLatin1String("L") && letter != QLatin1String("B"))
            return QStringLiteral("Love must be L (loved) or B (banned).");
    }
    return { };
}

QList<TagChanges> buildEdits(const QList<TrackTags> &tracks, const QMap<QString, QString> &edits)
{
    // The edits E-CHECK accepts, by catalogue name, with the values they stand for.
    QMap<QString, QStringList> wanted;
    for (auto it = edits.constBegin(); it != edits.constEnd(); ++it) {
        if (!editProblem(it.key(), it.value()).isEmpty())
            continue;
        const QString name = catalogueName(it.key());
        QStringList values = valuesFromText(name, it.value());
        if (name == QLatin1String("Track#") || name == QLatin1String("Track Count")
            || name == QLatin1String("Disc#") || name == QLatin1String("Disc Count")) {
            for (QString &value : values)
                value = QString::number(value.toInt());
        } else if (name == QLatin1String("Rating")) {
            for (QString &value : values)
                value = ratingToString(value.toDouble());
        } else if (name == QLatin1String("Love")) {
            for (QString &value : values)
                value = value.toUpper();
        }
        wanted.insert(name, values);
    }

    // Name a field for a track only where its values would really change.
    QList<TagChanges> result;
    for (const TrackTags &track : tracks) {
        TagChanges change;
        for (auto it = wanted.constBegin(); it != wanted.constEnd(); ++it) {
            if (track.fields.value(it.key()) != it.value())
                change.fields.insert(it.key(), it.value());
        }
        result.append(change);
    }
    return result;
}

}
