#pragma once
#include <QDialog>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

#include "organise/organiser.h"

class QSettings;

// The Organise Files dialog: where the files go, what they will be called,
// a preview of every move, and the button that does it.
// Contract: docs/spec/organise-dialog.md (OD-x).
namespace linuxbee::ui {

class OrganiseDialog : public QDialog {
    Q_OBJECT
public:
    using Planner = std::function<QList<PlannedMove>(const QStringList &, const OrganiseOptions &)>;
    using Executor = std::function<OrganiseReport(
        const QList<PlannedMove> &, const OrganiseOptions &, const OrganiseProgress &)>;

    // `settings` is not owned and must outlive the dialog.
    OrganiseDialog(const QStringList &files, QSettings *settings, QWidget *parent = nullptr);
    ~OrganiseDialog() override;

    // By default linuxbee::planOrganise and linuxbee::executeOrganise.
    void setPlanner(Planner planner); // also refreshes the preview
    void setExecutor(Executor executor);

    QString templateText() const;
    OrganiseOptions options() const; // what the controls say now
    QList<PlannedMove> preview() const; // what the table shows

    // OD-RUN. False when there was nothing to do or the options cannot be used.
    bool run();

signals:
    void organised(const linuxbee::OrganiseReport &report);

private:
    struct Private;
    Private *d;
    // Added by plan 13 (private, not public API): asks the planner and fills
    // the table, the summary and the problem label (OD-TEMPLATE, OD-PREVIEW).
    void refreshPreview();
};

}
