#include "formatdetail.h"

#include <QDir>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QUrl>

namespace linuxbee::playlists {

QStringList decodeLines(const QByteArray &data, PlaylistFormat format)
{
    // Y-DECODE: a UTF-8 BOM at the start is dropped.
    QByteArray bytes = data;
    if (bytes.startsWith(QByteArrayLiteral("\xEF\xBB\xBF")))
        bytes.remove(0, 3);

    QString text;
    if (format == PlaylistFormat::M3u8) {
        // .m3u8 is UTF-8 by definition: a byte sequence that is not valid
        // becomes U+FFFD and the rest is kept.
        text = QString::fromUtf8(bytes);
    } else {
        // .m3u and .pls: UTF-8 when all of the bytes are valid UTF-8.
        QStringDecoder decoder(QStringDecoder::Utf8);
        const QString asUtf8 = decoder.decode(bytes);
        text = decoder.hasError() ? QString::fromLatin1(bytes) : asUtf8;
    }

    QStringList lines;
    QString current;
    for (const QChar ch : text) {
        if (ch == u'\n' || ch == u'\r') {
            lines.append(current.trimmed());
            current.clear();
        } else
            current.append(ch);
    }
    lines.append(current.trimmed());
    return lines;
}

QString resolveLocation(const QString &raw, const QString &baseFolder)
{
    // Y-PATH rule 1: a file URL, percent-decoded.
    if (raw.startsWith(QStringLiteral("file://"), Qt::CaseInsensitive))
        return QDir::cleanPath(QUrl(raw).toLocalFile());

    // Y-PATH rule 2: any other URL is kept exactly. A scheme is at least two
    // of letters, digits, '+', '-' or '.', then "://".
    static const QRegularExpression kScheme(QStringLiteral("^[A-Za-z0-9+.-]{2,}://"));
    if (kScheme.match(raw).hasMatch())
        return raw;

    // Y-PATH rule 3: a path with backslashes and no forward slash is a
    // Windows path; with a real slash in it nothing is guessed.
    QString path = raw;
    if (path.contains(QLatin1Char('\\')) && !path.contains(QLatin1Char('/')))
        path.replace(QLatin1Char('\\'), QLatin1Char('/'));

    // Y-PATH rule 4: an absolute path, including a Windows drive path that
    // cannot be opened here, cleaned and kept for the library's relink.
    const bool drive
        = path.size() >= 3 && path.at(0).isLetter() && path.at(1) == u':' && path.at(2) == u'/';
    if (path.startsWith(QLatin1Char('/')) || drive)
        return QDir::cleanPath(path);

    // Y-PATH rule 5: relative to the folder the playlist file is in.
    return QDir::cleanPath(QDir(baseFolder).filePath(path));
}

QString writtenLocation(const QString &location, const PlaylistWriteOptions &options)
{
    // Y-WRITE: relative only when asked, only with a folder to be relative
    // to, and only for a path that starts with '/'.
    if (!options.relativePaths || options.baseFolder.isEmpty()
        || !location.startsWith(QLatin1Char('/')))
        return location;
    return QDir(options.baseFolder).relativeFilePath(location);
}

}
