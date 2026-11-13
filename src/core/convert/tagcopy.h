#pragma once
#include <QString>
#include <QStringList>

#include "tags/tagreader.h"
#include "tags/tagwriter.h"

// Carries a source's tags and pictures over to a converted file (C-TAGS).
// Contract: docs/spec/convert.md.
namespace linuxbee::convert {

// Makes `target` carry `source`'s tags (C-TAGS). Returns an empty text on
// success, otherwise what went wrong. Entries that could not be carried over
// are appended to `warnings`.
QString copyTags(const TrackTags &source, const QString &target, bool pictures,
    const WriteOptions &options, QStringList *warnings);

}
