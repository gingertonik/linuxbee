#pragma once
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include "tags/tagreader.h"
#include "tags/tagwriter.h"

// The thinking behind the tag editor: what a field shows when several
// tracks are open at once, how typed text becomes values, and which
// changes follow for each track. Contract: docs/spec/tag-editor.md (E-x).
// Pure: nothing here reads or writes a file.
namespace linuxbee {

struct FieldState {
    enum class Kind {
        Blank, // no track has a value
        Same, // every track has the same value(s)
        Mixed, // anything else
    };
    Kind kind = Kind::Blank;
    QString text; // Same: the values joined with "; ". Otherwise empty.
};

// E-FIELDS: the fields the editor shows, in order.
QStringList tagEditorFields();
// E-SPLIT: whether a field holds a list of values.
bool isListField(const QString &fieldName);

// E-STATE
FieldState fieldState(const QList<TrackTags> &tracks, const QString &fieldName);

// E-SPLIT: the values typed text stands for. Empty list: remove the field.
QStringList valuesFromText(const QString &fieldName, const QString &text);

// E-CHECK: empty when the text can be saved to that field, otherwise one
// sentence saying what is wrong.
QString editProblem(const QString &fieldName, const QString &text);

// E-BUILD: one TagChanges per track, in order, naming only what really
// changes for that track. `edits` maps a field name to its typed text.
QList<TagChanges> buildEdits(const QList<TrackTags> &tracks, const QMap<QString, QString> &edits);

}
