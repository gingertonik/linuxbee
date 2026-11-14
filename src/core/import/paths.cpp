#include "itunesxml.h"

#include <QDir>
#include <QUrl>

// I-LOCATION and I-MAP. Pure string work; nothing here touches the disk.
namespace linuxbee {

namespace {

// "C:" or "C:/": a drive letter and a colon at the start.
bool hasDriveLetter(const QString &path)
{
    return path.size() >= 2 && path.at(1) == QLatin1Char(':') && path.at(0).isLetter();
}

// Cleans a path: no ".", "..", doubled or trailing slashes. A ".." at a root
// stays at that root ("/", "//" of a network path, "C:/"), so a rooted path
// never climbs above it and never loses its drive letter. QDir::cleanPath
// alone keeps "/.." and treats "C:" as an ordinary folder.
QString cleanKeepingNetwork(const QString &path)
{
    QString root;
    QString rest = path;
    if (path.startsWith(QStringLiteral("//"))) {
        root = QStringLiteral("//");
        rest = path.mid(2);
    } else if (hasDriveLetter(path) && (path.size() == 2 || path.at(2) == QLatin1Char('/'))) {
        root = path.left(2) + QLatin1Char('/');
        rest = path.mid(3);
    } else if (path.startsWith(QLatin1Char('/'))) {
        root = QStringLiteral("/");
        rest = path.mid(1);
    } else {
        return QDir::cleanPath(path);
    }
    QStringList parts;
    for (const QString &part : rest.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        if (part == QLatin1String("."))
            continue;
        if (part == QLatin1String("..")) {
            if (!parts.isEmpty())
                parts.removeLast();
            continue;
        }
        parts.append(part);
    }
    if (parts.isEmpty() && root.size() == 3)
        return root.left(2); // "C:/.." is "C:", as QDir::cleanPath gave
    return root + parts.join(QLatin1Char('/'));
}

// Removes trailing slashes, but keeps "/" and a drive root such as "C:/".
QString trimTrailingSlash(QString path)
{
    while (path.size() > 1 && path.endsWith(QLatin1Char('/'))
        && !(path.size() == 3 && hasDriveLetter(path)))
        path.chop(1);
    return path;
}

// Removes every trailing slash, so that "/" becomes "" and "C:/" becomes "C:",
// and a folder can always be joined with "/" in front of the rest.
QString withoutTrailingSlashes(QString path)
{
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    return path;
}

QString slashed(QString path)
{
    return path.replace(QLatin1Char('\\'), QLatin1Char('/'));
}

}

QString pathFromItunesLocation(const QString &location)
{
    const QString text = location.trimmed();
    const QString scheme = QStringLiteral("file:");
    if (text.left(scheme.size()).compare(scheme, Qt::CaseInsensitive) != 0)
        return { };

    QString rest = text.mid(scheme.size());
    QString prefix; // "//host" for a network path, else empty
    if (rest.startsWith(QStringLiteral("//"))) {
        rest = rest.mid(2);
        const int slash = rest.indexOf(QLatin1Char('/'));
        const QString host = slash < 0 ? rest : rest.left(slash);
        rest = slash < 0 ? QString() : rest.mid(slash);
        if (!host.isEmpty() && host.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) != 0)
            prefix = QStringLiteral("//") + host;
    }

    // Percent-decoding reads the bytes as UTF-8 and leaves "+" alone.
    QString path = QUrl::fromPercentEncoding(rest.toUtf8());
    // "/C:/Music" is "C:/Music".
    if (path.startsWith(QLatin1Char('/')) && hasDriveLetter(path.mid(1)))
        path = path.mid(1);

    return trimTrailingSlash(cleanKeepingNetwork(prefix + path));
}

QString mapImportedPath(const QString &path, const PathMapping &mapping)
{
    const QString cleaned = cleanKeepingNetwork(slashed(path));
    for (const auto &pair : mapping) {
        if (pair.first.isEmpty())
            continue; // an empty "from" fits nothing

        const QString from = withoutTrailingSlashes(cleanKeepingNetwork(slashed(pair.first)));
        const QString folder = from + QLatin1Char('/');
        // Drive letters and network hosts compare without case; Unix folders do not.
        const bool ignoreCase = hasDriveLetter(from) || from.startsWith(QStringLiteral("//"));
        const Qt::CaseSensitivity sensitivity
            = ignoreCase ? Qt::CaseInsensitive : Qt::CaseSensitive;
        // The folder itself does not fit: only what lies under it.
        if (cleaned.left(folder.size()).compare(folder, sensitivity) != 0)
            continue;

        const QString to = withoutTrailingSlashes(cleanKeepingNetwork(slashed(pair.second)));
        return cleanKeepingNetwork(to + QLatin1Char('/') + cleaned.mid(folder.size()));
    }
    return cleaned;
}

}
