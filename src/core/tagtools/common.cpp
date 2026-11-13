#include "common.h"

namespace linuxbee::tagtools {

QString canonicalField(const QString &name)
{
    for (const QString &field : tagFieldNames()) {
        if (field.compare(name, Qt::CaseInsensitive) == 0)
            return field;
    }
    return { };
}

// G-CHANGES: a field is named only when its cleaned values differ from the
// values the track has now. An empty list names a field to remove.
void setIfChanged(
    TagChanges *changes, const TrackTags &tags, const QString &field, const QStringList &values)
{
    QStringList cleaned;
    for (const QString &value : values) {
        const QString trimmed = value.trimmed();
        if (!trimmed.isEmpty())
            cleaned.append(trimmed);
    }
    if (cleaned == tags.fields.value(field))
        return;
    changes->fields.insert(field, cleaned);
}

}
