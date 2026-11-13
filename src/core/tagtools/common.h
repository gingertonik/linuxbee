#pragma once
#include <QString>
#include <QStringList>

#include "tags/tagreader.h"
#include "tags/tagwriter.h"

// Helpers shared by the tagging tools. Contract: docs/spec/tagging-tools.md, G-CHANGES.
namespace linuxbee::tagtools {

// The catalogue's spelling of a field name, or an empty text.
QString canonicalField(const QString &name);
// Names `field` in `changes` when `values` (trimmed, empties dropped)
// differ from what `tags` has.
void setIfChanged(
    TagChanges *changes, const TrackTags &tags, const QString &field, const QStringList &values);

}
