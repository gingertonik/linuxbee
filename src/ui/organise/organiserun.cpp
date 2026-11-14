#include "organise/organisedialog_p.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QTreeWidget>
#include <QTreeWidgetItem>

namespace linuxbee::ui {

namespace {

// OD-RUN: while a run is under way, Escape and the window's own close ask the
// run to stop, and the dialog stays open. Installed for the length of the
// executor call only; with no run under way the events pass through.
class RunGuard : public QObject {
public:
    RunGuard(QWidget *dialog, const bool *running, bool *stopRequested)
        : dialog_(dialog)
        , running_(running)
        , stopRequested_(stopRequested)
    {
        dialog_->installEventFilter(this);
    }
    ~RunGuard() override { dialog_->removeEventFilter(this); }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (!*running_)
            return QObject::eventFilter(watched, event);
        if (event->type() == QEvent::KeyPress
            && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            event->accept();
            *stopRequested_ = true;
            return true;
        }
        if (event->type() == QEvent::Close) {
            event->ignore(); // close() then reports false, as the dialog stays open
            *stopRequested_ = true;
            return true;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QWidget *dialog_;
    const bool *running_;
    bool *stopRequested_;
};

}

// OD-RUN: hand the plan shown to the executor, follow its progress, then
// report what happened to each file.
bool OrganiseDialog::run()
{
    if (!d->runButton->isEnabled() || d->running)
        return false;

    // OD-OPTIONS: what a run uses is remembered when the run starts.
    const OrganiseOptions opts = options();
    if (d->settings) {
        d->settings->setValue(QStringLiteral("organise/target"), opts.targetRoot);
        d->settings->setValue(QStringLiteral("organise/template"), templateText());
        d->settings->setValue(QStringLiteral("organise/copy"), d->copyRadio->isChecked());
        d->settings->setValue(QStringLiteral("organise/replace"), d->replaceCheck->isChecked());
        d->settings->setValue(
            QStringLiteral("organise/otherFiles"), d->otherFilesCheck->isChecked());
        d->settings->setValue(
            QStringLiteral("organise/removeEmpty"), d->removeEmptyCheck->isChecked());
        d->settings->setValue(
            QStringLiteral("organise/skipMissing"), d->skipMissingCheck->isChecked());
        d->settings->sync();
    }

    const QList<PlannedMove> plan = d->plan;
    d->running = true;
    d->stopRequested = false;
    d->runButton->setEnabled(false);
    d->progressBar->setValue(0);
    d->progressBar->setVisible(true);

    const OrganiseProgress progress = [this](int index, int count) {
        d->progressBar->setMaximum(count);
        d->progressBar->setValue(index);
        // Keeps the window answering, so that Close can be pressed during the run.
        QCoreApplication::processEvents();
        return !d->stopRequested;
    };
    OrganiseReport report;
    {
        RunGuard guard(this, &d->running, &d->stopRequested);
        report = d->executor(plan, opts, progress);
    }

    d->running = false;
    d->progressBar->setVisible(false);

    // The planned moves come first in the report; the table shows those rows.
    // A row the run never came to, because it was stopped first, shows Not reached.
    for (qsizetype i = 0; i < plan.size(); ++i) {
        QString word = tr("Not reached");
        if (i < report.outcomes.size()) {
            const MoveOutcome &outcome = report.outcomes.at(i);
            switch (outcome.status) {
            case MoveOutcome::Status::Done:
                word = tr("Done");
                break;
            case MoveOutcome::Status::Failed:
                word = tr("Failed: %1").arg(outcome.message);
                break;
            case MoveOutcome::Status::Skipped:
                word = tr("Skipped: %1").arg(outcome.message);
                break;
            }
        }
        if (QTreeWidgetItem *row = d->previewTable->topLevelItem(static_cast<int>(i)))
            row->setText(2, word);
    }

    QString summary = tr("%1 done, %2 skipped, %3 failed")
                          .arg(report.done)
                          .arg(report.skipped)
                          .arg(report.failed);
    if (report.cancelled)
        summary += tr(" (stopped)");
    d->summaryLabel->setText(summary);

    emit organised(report);

    if (report.failed == 0 && !report.cancelled) {
        accept();
    } else {
        // The table now shows results, not a plan. Changing an option makes a fresh preview.
        d->runButton->setEnabled(false);
    }
    return true;
}

}
