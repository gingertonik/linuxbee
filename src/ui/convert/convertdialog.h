#pragma once
#include <QDialog>
#include <QList>
#include <QString>
#include <QStringList>

#include "convert/convertbatch.h"

class QSettings;

// The Convert Format dialog: which format, where to, what to do with files
// that are already there, and a table that follows every file through the
// run. Contract: docs/spec/convert-dialog.md (CD-x).
namespace linuxbee::ui {

class ConvertDialog : public QDialog {
    Q_OBJECT
public:
    // `settings` is not owned and must outlive the dialog.
    ConvertDialog(const QStringList &files, QSettings *settings, QWidget *parent = nullptr);
    ~ConvertDialog() override;

    BatchOptions options() const; // what the controls say now
    QList<ConvertJob> plan() const; // what the table shows
    ConvertBatch *batch() const; // the dialog's own; never null

    // CD-RUN. False when there is nothing to convert or a run is under way.
    bool run();

signals:
    // After a run: the files that now hold a result (converted, or tags updated).
    void converted(const QStringList &outputPaths);

public slots:
    // CD-RUN: during a run this asks the run to stop instead of closing.
    void reject() override;

private:
    struct Private;
    Private *d;
};

}
