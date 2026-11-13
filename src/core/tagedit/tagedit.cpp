#include "tagedit/tagedit.h"

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
    Q_UNUSED(fieldName);
    Q_UNUSED(text);
    return { };
}

QString editProblem(const QString &fieldName, const QString &text)
{
    Q_UNUSED(fieldName);
    Q_UNUSED(text);
    return { };
}

QList<TagChanges> buildEdits(const QList<TrackTags> &tracks, const QMap<QString, QString> &edits)
{
    Q_UNUSED(tracks);
    Q_UNUSED(edits);
    return { };
}

}
