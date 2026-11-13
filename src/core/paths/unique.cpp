#include "pathbuilder.h"

#include "component.h"

#include <QString>

namespace linuxbee {

QString uniqueRelativePath(const QString &relativePath,
    const std::function<bool(const QString &)> &exists, const PathRules &rules)
{
    if (!exists(relativePath))
        return relativePath;

    const int slash = relativePath.lastIndexOf(QLatin1Char('/'));
    const QString folder = relativePath.left(slash + 1); // keeps its '/', empty in the root
    const QString name = relativePath.mid(slash + 1);

    // P-DUP: the extension is what follows the last dot of the last
    // component, and only when that dot is not the first character. Dots in
    // folder names do not count.
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    const QString stem = dot > 0 ? name.left(dot) : name;
    const QString extension = dot > 0 ? name.mid(dot) : QString();

    // W-O: duplicates get `_#` appended; P-DUP: # starts at 1 and stops
    // after 9999. exists is called once per candidate, no more.
    for (int n = 1; n <= 9999; ++n) {
        const QString suffix = QStringLiteral("_") + QString::number(n);
        const int budget
            = rules.maxComponentBytes - suffix.toUtf8().size() - extension.toUtf8().size();
        const QString candidate = folder + paths::fitToBytes(stem, budget) + suffix + extension;
        if (!exists(candidate))
            return candidate;
    }
    return { };
}

} // namespace linuxbee
