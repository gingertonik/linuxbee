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

}
