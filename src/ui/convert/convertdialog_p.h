#pragma once

// Private state of ConvertDialog, shared by convertdialog.cpp (the controls,
// the options and the table) and convertrun.cpp (the run). Contract:
// docs/spec/convert-dialog.md (CD-x).

#include "convert/convertdialog.h"

#include <QList>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTreeWidget;

namespace linuxbee::ui {

struct ConvertDialog::Private {
    ConvertDialog *q = nullptr;
    QStringList files;
    QSettings *settings = nullptr;
    ConvertBatch *batch = nullptr;

    QComboBox *presetCombo = nullptr;
    QLineEdit *folderEdit = nullptr;
    QPushButton *browseButton = nullptr;
    QComboBox *ifExistsCombo = nullptr;
    QSpinBox *parallelSpin = nullptr;
    QCheckBox *copyTagsCheck = nullptr;
    QCheckBox *copyPicturesCheck = nullptr;
    QTreeWidget *jobTable = nullptr;
    QLabel *summaryLabel = nullptr;
    QProgressBar *progressBar = nullptr;
    QPushButton *runButton = nullptr;
    QPushButton *closeButton = nullptr;

    void loadSettings(); // convertdialog.cpp
    void refreshTable(); // convertdialog.cpp
    void saveSettings(); // convertrun.cpp
    void setRunning(bool on); // convertrun.cpp
    void connectBatch(); // convertrun.cpp
};

}
