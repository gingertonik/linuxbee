#pragma once
#include <QMap>
#include <QString>
#include <QStringList>

#include "tagwriter.h"

// Validation and planning for writeTags. Qt types only; no TagLib here.
// Contract: docs/spec/tag-writing.md sections 1 and 2.
namespace linuxbee::tags {

struct CheckedChanges {
    QMap<QString, QStringList> fields; // canonical names, cleaned values
    QMap<QString, QStringList> other; // upper-cased keys, cleaned values
    QStringList problems;
};

// W-FIELDS and W-INVALID. Every refused entry gives one problem line, which
// starts with the field name or key as the caller wrote it, then a colon.
CheckedChanges checkChanges(const TagChanges &changes);

}
