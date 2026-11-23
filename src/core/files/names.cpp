#include "names.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QStringDecoder>

#include <cerrno>

#include <dirent.h>
#include <sys/stat.h>

namespace linuxbee {

namespace {

// A decoder that reports errors, not a round trip through QString (as musicfiles.cpp does).
bool isValidUtf8(const QByteArray &name)
{
    QStringDecoder decoder(QStringDecoder::Utf8);
    [[maybe_unused]] const QString decoded = decoder(name); // without this use, no error is seen
    return !decoder.hasError();
}

// The number of UTF-8 bytes of the character that starts at `at`, and how many QChars it takes.
qsizetype letterAt(const QString &text, qsizetype at, qsizetype *units)
{
    const bool pair
        = text.at(at).isHighSurrogate() && at + 1 < text.size() && text.at(at + 1).isLowSurrogate();
    *units = pair ? 2 : 1;
    return text.mid(at, *units).toUtf8().size();
}

}

QString scratchTail(const QString &fileName, int headBytes)
{
    constexpr qsizetype kLongest = 255;
    const QByteArray whole = fileName.toUtf8();
    if (whole.size() + headBytes <= kLongest)
        return fileName;
    // The extension, from the last dot, when it is 16 bytes or less.
    QString extension;
    const qsizetype dot = fileName.lastIndexOf(QLatin1Char('.'));
    if (dot > 0 && fileName.mid(dot).toUtf8().size() <= 16)
        extension = fileName.mid(dot);
    const QString mark = QLatin1Char('~')
        + QString::fromLatin1(QCryptographicHash::hash(whole, QCryptographicHash::Sha1).toHex())
              .left(8)
        + extension;
    const qsizetype room = kLongest - headBytes - mark.toUtf8().size();
    QString head;
    qsizetype used = 0;
    for (qsizetype at = 0; at < fileName.size();) {
        qsizetype units = 1;
        const qsizetype bytes = letterAt(fileName, at, &units);
        if (used + bytes > room)
            break;
        head += fileName.mid(at, units);
        used += bytes;
        at += units;
    }
    return head + mark;
}

NotUtf8 notUtf8Where(const QString &path)
{
    const QStringList parts = QDir::cleanPath(QFileInfo(path).absoluteFilePath())
                                  .split(QLatin1Char('/'), Qt::SkipEmptyParts);
    QString folder = QStringLiteral("/");
    QString here;
    for (qsizetype i = 0; i < parts.size(); ++i) {
        here += QLatin1Char('/') + parts.at(i);
        struct stat st;
        if (::lstat(QFile::encodeName(here).constData(), &st) == 0) {
            folder = here;
            continue;
        }
        if (errno != ENOENT)
            return NotUtf8::No; // not "not there": a folder that cannot be searched, a file as a
                                // folder
        // The first name that is not there. Look for it, by its bytes, in the folder above it.
        DIR *dir = ::opendir(QFile::encodeName(folder).constData());
        if (!dir)
            return NotUtf8::No;
        bool found = false;
        while (const dirent *entry = ::readdir(dir)) {
            const QByteArray name(entry->d_name);
            if (isValidUtf8(name))
                continue;
            if (QString::fromUtf8(name) == parts.at(i)) {
                found = true;
                break;
            }
        }
        ::closedir(dir);
        if (!found)
            return NotUtf8::No;
        return i + 1 == parts.size() ? NotUtf8::TheFile : NotUtf8::AFolder;
    }
    return NotUtf8::No;
}

QString notUtf8Words(NotUtf8 where)
{
    switch (where) {
    case NotUtf8::TheFile:
        return QStringLiteral("its name is not valid UTF-8");
    case NotUtf8::AFolder:
        return QStringLiteral("the name of a folder in its path is not valid UTF-8");
    case NotUtf8::No:
        break;
    }
    return QString();
}

}
