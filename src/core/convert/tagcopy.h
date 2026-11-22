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
// With `update` (C-UPDATE, the existing file of UpdateTags) it only adds and
// changes: what the source lacks is left, the source's pictures go on only
// when it has some, and a target whose length differs from the source's by
// more than 500 ms is refused with nothing written.
QString copyTags(const TrackTags &source, const QString &target, bool pictures,
    const WriteOptions &options, QStringList *warnings, bool update = false);

}
