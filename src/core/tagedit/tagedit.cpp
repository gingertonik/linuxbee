#include "tagedit/tagedit.h"

// Stub. Plan 12 replaces every body in this file.
namespace linuxbee {

QStringList tagEditorFields()
{
    return { };
}

bool isListField(const QString &fieldName)
{
    Q_UNUSED(fieldName);
    return false;
}

FieldState fieldState(const QList<TrackTags> &tracks, const QString &fieldName)
{
    Q_UNUSED(tracks);
    Q_UNUSED(fieldName);
    return { };
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
