#pragma once
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include "tagreader.h"

// R-CATALOGUE: the fixed table of linuxBee field names and the TagLib property
// keys each one is read from. Qt types only; TagLib never reaches this header.
namespace linuxbee::tags {

struct FieldKeys {
    QString field;
    QStringList keys; // upper-case TagLib property keys, first with a value wins
};

// Every catalogue field, in display order (R-CATALOGUE). Rating has no keys
// here: it is read through R-RATING, not as a plain text key.
const QList<FieldKeys> &fieldTable();

// Moves catalogue fields out of `properties` (upper-cased key -> values) into
// `tags.fields`, the rest into `tags.other`. `id3` selects R-GROUPING.
void assignFields(QMap<QString, QStringList> properties, bool id3, TrackTags &tags);

}
