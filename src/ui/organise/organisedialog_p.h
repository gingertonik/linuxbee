#pragma once
#include "organise/organisedialog.h"

class QCheckBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QTreeWidget;

// The state shared by organisedialog.cpp and organisepreview.cpp (and
// organiserun.cpp). Private to the dialog; not part of the public API.
namespace linuxbee::ui {

struct OrganiseDialog::Private {
    QSettings *settings = nullptr;
    QStringList files;
    Planner planner;
    Executor executor;

    QLineEdit *targetEdit = nullptr;
    QPushButton *browseButton = nullptr;
    QLineEdit *templateEdit = nullptr;
    QLabel *problemLabel = nullptr;
    QRadioButton *moveRadio = nullptr;
    QRadioButton *copyRadio = nullptr;
    QCheckBox *replaceCheck = nullptr;
    QCheckBox *otherFilesCheck = nullptr;
    QCheckBox *removeEmptyCheck = nullptr;
    QCheckBox *skipMissingCheck = nullptr;
    QTreeWidget *previewTable = nullptr;
    QLabel *summaryLabel = nullptr;
    QProgressBar *progressBar = nullptr;
    QDialogButtonBox *buttons = nullptr;
    QPushButton *runButton = nullptr;
    QPushButton *closeButton = nullptr;

    // OD-PREVIEW: the plan the table shows, as the planner last answered.
    QList<PlannedMove> plan;
};

}
