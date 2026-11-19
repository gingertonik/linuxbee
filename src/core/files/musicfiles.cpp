#include "musicfiles.h"

#include <QDir>
#include <QFileInfo>
#include <QStringConverter>

#include <dirent.h>
#include <sys/stat.h>

#include <cstring>
#include <set>
#include <utility>

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

// L-NAMES: a name is valid when its bytes are UTF-8, checked with a decoder
// that reports errors and not by a round trip through QString.
bool isValidUtf8(const QByteArray &name)
{
    QStringDecoder decoder(QStringDecoder::Utf8);
    [[maybe_unused]] const QString decoded = decoder(name); // without this use, no error is seen
    return !decoder.hasError();
}

// L-NAMES: what the walk has found so far. A folder is told apart by its
// device and inode, so one named or reached twice is walked once.
struct UnreadableWalk {
    std::set<std::pair<dev_t, ino_t>> visited;
    int names = 0;
    QStringList folders;
};

// L-NAMES: counts the entries of one folder whose names are not valid UTF-8,
// and walks below it the way the organiser's walk does: hidden folders and
// symbolic links are not gone into. A folder with an invalid name is counted
// by its parent and not gone into, since no path to it can be made in text.
void walkForUnreadable(const QString &folder, UnreadableWalk *walk)
{
    const QByteArray path = folder.toUtf8();
    struct stat rootInfo;
    if (::stat(path.constData(), &rootInfo) != 0 || !S_ISDIR(rootInfo.st_mode))
        return;
    if (!walk->visited.insert({ rootInfo.st_dev, rootInfo.st_ino }).second)
        return;
    DIR *dir = ::opendir(path.constData());
    if (!dir)
        return;
    int here = 0;
    while (const dirent *entry = ::readdir(dir)) {
        if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0)
            continue;
        const QByteArray name(entry->d_name);
        if (!isValidUtf8(name)) {
            ++walk->names;
            ++here;
            continue;
        }
        if (name.startsWith('.'))
            continue;
        const QByteArray childPath = path + '/' + name;
        struct stat childInfo;
        if (::lstat(childPath.constData(), &childInfo) == 0 && S_ISDIR(childInfo.st_mode))
            walkForUnreadable(folder + QLatin1Char('/') + QString::fromUtf8(name), walk);
    }
    ::closedir(dir);
    if (here > 0)
        walk->folders.append(folder);
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

UnreadableNames unreadableNamesBelow(const QStringList &folders)
{
    UnreadableWalk walk;
    for (const QString &folder : folders)
        walkForUnreadable(folder, &walk);
    walk.folders.removeDuplicates();
    walk.folders.sort();
    return { walk.names, walk.folders };
}

}
