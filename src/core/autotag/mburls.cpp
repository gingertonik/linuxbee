#include "musicbrainz.h"

#include "version.h"

#include <algorithm>

#include <QRegularExpression>
#include <QStringList>

namespace linuxbee {

namespace {

const QString kReleaseApi = QStringLiteral("https://musicbrainz.org/ws/2/release/");
const QString kCoverArt = QStringLiteral("https://coverartarchive.org/release/");
const QString kProjectAddress = QStringLiteral("https://github.com/gingertonik/linuxbee");

// M-QUERY: 36 characters, lower-case hex in groups of 8, 4, 4, 4 and 12.
// \A and \z, not ^ and $: $ also matches before a final newline.
bool isReleaseId(const QString &id)
{
    static const QRegularExpression shape(
        QStringLiteral("\\A[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\\z"));
    return shape.match(id).hasMatch();
}

// One field phrase of the search syntax. A backslash is escaped first, so the
// backslash added in front of a double quote is not escaped again.
QString phrase(const QString &field, const QString &value)
{
    QString escaped = value;
    escaped.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    escaped.replace(QStringLiteral("\""), QStringLiteral("\\\""));
    return field + QStringLiteral(":\"") + escaped + QStringLiteral("\"");
}

// Percent-encodes a query value. QUrlQuery::addQueryItem would leave & + # and =
// inside a value, so the query is built here and set in strict mode.
QString encoded(const QString &value)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(value));
}

}

QString musicBrainzUserAgent()
{
    return QStringLiteral("linuxBee/") + versionString() + QStringLiteral(" ( ") + kProjectAddress
        + QStringLiteral(" )");
}

QUrl releaseSearchUrl(const QString &artist, const QString &album, int limit)
{
    const QString artistText = artist.trimmed();
    const QString albumText = album.trimmed();
    QStringList parts;
    if (!artistText.isEmpty())
        parts.append(phrase(QStringLiteral("artist"), artistText));
    if (!albumText.isEmpty())
        parts.append(phrase(QStringLiteral("release"), albumText));
    if (parts.isEmpty())
        return { };

    const int held = std::clamp(limit, 1, 100);
    const QString query = parts.join(QStringLiteral(" AND "));
    QUrl url(kReleaseApi);
    url.setQuery(QStringLiteral("query=") + encoded(query) + QStringLiteral("&fmt=json&limit=")
            + QString::number(held),
        QUrl::StrictMode);
    return url;
}

QUrl releaseLookupUrl(const QString &releaseId)
{
    if (!isReleaseId(releaseId))
        return { };
    return QUrl(kReleaseApi + releaseId
        + QStringLiteral("?inc=recordings+artist-credits+labels+release-groups&fmt=json"));
}

QUrl frontCoverUrl(const QString &releaseId, int size)
{
    if (!isReleaseId(releaseId))
        return { };
    if (size == 0)
        return QUrl(kCoverArt + releaseId + QStringLiteral("/front"));
    if (size != 250 && size != 500 && size != 1200)
        return { };
    return QUrl(kCoverArt + releaseId + QStringLiteral("/front-") + QString::number(size));
}

}
