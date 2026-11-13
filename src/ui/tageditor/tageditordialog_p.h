#pragma once
#include "tageditor/tageditordialog.h"

#include "tagedit/tagedit.h"

#include <QList>
#include <QMap>
#include <QSet>
#include <QString>

class QLabel;
class QLineEdit;
class QPushButton;

// The state behind TagEditorDialog, shared by its form (tageditordialog.cpp)
// and its save step (tageditorsave.cpp).
namespace linuxbee::ui {

struct TagEditorDialog::Private {
    QList<EditTarget> targets;
    TagEditorDialog::Saver saver;
    // What each field showed when the dialog opened, by catalogue name.
    QMap<QString, FieldState> states;
    QMap<QString, QLineEdit *> editors;
    QLabel *summary = nullptr;
    QLabel *problem = nullptr;
    QPushButton *saveButton = nullptr;
    QPushButton *cancelButton = nullptr;
    // Indexes of the targets whose files were written; a later Save leaves them alone.
    QSet<int> finished;
};

}
