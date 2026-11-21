#include "tagreader.h"

#include "catalogue.h"

#include <QSet>

namespace linuxbee {

QString TrackTags::first(const QString &fieldName) const
{
    const auto it = fields.constFind(fieldName);
    if (it == fields.constEnd() || it->isEmpty())
        return { };
    return it->first();
}

QString TrackTags::joined(const QString &fieldName) const
{
    return fields.value(fieldName).join(QStringLiteral("; "));
}

TrackTagSource::TrackTagSource(const TrackTags &tags)
    : m_tags(tags)
{
}

// R-SOURCE: a catalogue field name, ignoring case, is known; its value is the
// joined values, blank when it has none. Any other name is unknown.
std::optional<QString> TrackTagSource::value(const QString &fieldName) const
{
    // A name written as the catalogue writes it is found by one lookup in a set made once;
    // any other spelling is looked for ignoring case, as before.
    static const QSet<QString> exact = [] {
        const QStringList names = tagFieldNames();
        return QSet<QString>(names.cbegin(), names.cend());
    }();
    if (exact.contains(fieldName))
        return m_tags.joined(fieldName);

    for (const tags::FieldKeys &entry : tags::fieldTable()) {
        if (entry.field.compare(fieldName, Qt::CaseInsensitive) == 0)
            return m_tags.joined(entry.field);
    }
    return std::nullopt;
}

}
