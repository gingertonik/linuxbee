#pragma once
#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include "tags/tagreader.h"

// Which picture stands for a track. Contract: docs/spec/covers.md
// (CV-EMBEDDED, CV-FILE, CV-FIND). Reads files; writes nothing.
namespace linuxbee {

struct Cover {
    enum class Source { None, Embedded, File };
    Source source = Source::None;
    QString file; // Embedded: the music file. File: the picture file.
    QString mimeType;
    QString type; // Embedded: the picture's type name. File: empty.
    qsizetype byteCount = 0;
    QByteArray data; // empty when CoverOptions::data is false
};

struct CoverOptions {
    QList<Cover::Source> order = { Cover::Source::Embedded, Cover::Source::File };
    bool data = true;
};

// CV-EMBEDDED: the position of the cover among a file's pictures; -1: none.
int chooseEmbedded(const QList<Picture> &pictures);

// CV-FILE: the names looked for in a folder, in order.
QStringList coverNames();
// CV-FILE: the folder's cover file; empty: none.
QString coverFileIn(const QString &folder);

// CV-FIND
Cover findCover(const QString &musicFile, const CoverOptions &options = { });

}
