#include "musicfiles.h"

#include <QDir>
#include <QFileInfo>

namespace linuxbee {

namespace {

// O-MUSIC: the extensions that make a file music, in lower case.
const QStringList kMusicExtensions = {
    QStringLiteral("mp3"),
    QStringLiteral("flac"),
    QStringLiteral("ogg"),
    QStringLiteral("oga"),
    QStringLiteral("opus"),
    QStringLiteral("m4a"),
    QStringLiteral("m4b"),
    QStringLiteral("mp4"),
    QStringLiteral("aac"),
    QStringLiteral("wav"),
    QStringLiteral("wv"),
    QStringLiteral("ape"),
    QStringLiteral("mpc"),
    QStringLiteral("wma"),
    QStringLiteral("aiff"),
    QStringLiteral("aif"),
    QStringLiteral("tta"),
    QStringLiteral("dsf"),
};

// Appends the music files directly in `folder` (and, with `recursive`, below
// it) to `found`. QDir leaves out hidden entries unless asked for them. A
// symbolic link to a folder is not followed.
void collectMusicFiles(const QString &folder, bool recursive, QStringList *found)
{
    const QFileInfoList entries
        = QDir(folder).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        if (entry.isDir()) {
            if (recursive && !entry.isSymLink())
                collectMusicFiles(entry.absoluteFilePath(), recursive, found);
        } else if (hasMusicExtension(entry.fileName())) {
            found->append(entry.absoluteFilePath());
        }
    }
}

}

bool hasMusicExtension(const QString &fileName)
{
    // Only the last path component counts: a dot in a folder name is not an extension.
    const QString name = fileName.mid(fileName.lastIndexOf(QLatin1Char('/')) + 1);
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    // No dot, or the only dot is the first character: the name has no extension.
    if (dot <= 0)
        return false;
    return kMusicExtensions.contains(name.mid(dot + 1).toLower());
}

QStringList findMusicFiles(const QString &folder, bool recursive)
{
    // An empty name would mean the working directory to QDir; it means nothing here.
    if (folder.isEmpty())
        return { };
    QStringList found;
    collectMusicFiles(QDir::cleanPath(QFileInfo(folder).absoluteFilePath()), recursive, &found);
    found.sort();
    return found;
}

}
