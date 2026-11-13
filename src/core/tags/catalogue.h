#pragma once
#include <QList>
#include <QString>
#include <QStringList>

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

}
