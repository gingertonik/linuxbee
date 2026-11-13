#include "pathbuilder.h"

// Stub. Plan 02 replaces every body in this file.
namespace linuxbee {

QHash<QChar, QString> PathRules::defaultCharacterMap()
{
    return { };
}

BuiltPath buildRelativePath(const Template &pathTemplate, const TagSource &tags,
    const QString &extension, const PathRules &rules)
{
    Q_UNUSED(pathTemplate);
    Q_UNUSED(tags);
    Q_UNUSED(extension);
    Q_UNUSED(rules);
    return { };
}

QString cleanComponent(const QString &text, const PathRules &rules)
{
    Q_UNUSED(text);
    Q_UNUSED(rules);
    return { };
}

QString uniqueRelativePath(const QString &relativePath,
    const std::function<bool(const QString &)> &exists, const PathRules &rules)
{
    Q_UNUSED(relativePath);
    Q_UNUSED(exists);
    Q_UNUSED(rules);
    return { };
}

}
