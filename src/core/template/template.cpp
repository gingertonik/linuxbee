#include "template.h"

namespace linuxbee {

struct Template::Node { };

Template Template::parse(const QString &text)
{
    Q_UNUSED(text);
    Template t;
    t.m_error = { 0, QStringLiteral("not implemented") };
    return t;
}

bool Template::isValid() const
{
    return m_error.position < 0 && m_root != nullptr;
}

TemplateError Template::error() const
{
    return m_error;
}

QString Template::evaluate(const TagSource &tags, const TemplateOptions &options) const
{
    Q_UNUSED(tags);
    Q_UNUSED(options);
    return { };
}

QStringList Template::referencedFields() const
{
    return { };
}

}
