#include "organise/organisedialog_p.h"

#include <QCoreApplication>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QCheckBox>

namespace linuxbee::ui {

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
    const OrganiseReport report = d->executor(plan, opts, progress);

    d->running = false;
    d->progressBar->setVisible(false);

    // The planned moves come first in the report; the table shows those rows.
    for (qsizetype i = 0; i < plan.size() && i < report.outcomes.size(); ++i) {
        const MoveOutcome &outcome = report.outcomes.at(i);
        QString word;
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
