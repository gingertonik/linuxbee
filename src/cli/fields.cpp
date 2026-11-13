#include "fields.h"

namespace linuxbee::cli {

std::optional<QString> FieldSource::value(const QString &fieldName) const
{
    const auto found = m_fields.constFind(fieldName);
    if (found == m_fields.constEnd())
        return std::nullopt;
    return found.value();
}

void FieldSource::set(const QString &name, const QString &value)
{
    m_fields.insert(name, value);
}

bool readFields(const QStringList &specifications, FieldSource *fields)
{
    for (const QString &specification : specifications) {
        const qsizetype split = specification.indexOf(QLatin1Char('='));
        if (split <= 0)
            return false;
        fields->set(specification.left(split), specification.mid(split + 1));
    }
    return true;
}

}
