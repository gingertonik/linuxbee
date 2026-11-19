#pragma once
#include <QString>
#include <QStringList>

// Which files count as music, by name. Contract: docs/spec/organise.md, O-MUSIC.
namespace linuxbee {

// True when the name ends in one of the music extensions (case ignored).
bool hasMusicExtension(const QString &fileName);

// The music files in `folder`, as absolute paths in ascending order. With
// `recursive` the folders below it are searched too. Hidden files and
// folders (names starting with a dot) are skipped. Symbolic links to folders
// are not followed.
QStringList findMusicFiles(const QString &folder, bool recursive = true);

// L-NAMES (library.md) and O-CLI: the entries below `folders` whose own names are not valid
// UTF-8, files and folders alike. `names` counts them, `folders` holds each folder that
// has any, once, in ascending order. Such a folder is counted by its parent and not gone
// into; hidden folders and links are not gone into; a folder reached twice is walked once.
// Each folder is a clean absolute path.
struct UnreadableNames {
    int names = 0;
    QStringList folders;
};
UnreadableNames unreadableNamesBelow(const QStringList &folders);

}
