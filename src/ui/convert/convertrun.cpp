#include "convert/convertdialog_p.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTreeWidget>
#include <QWidget>

namespace linuxbee::ui {

namespace {

// CD-RUN 4: the Status text a finished row shows.
QString statusText(const ConvertResult &result)
{
    switch (result.status) {
    case ConvertResult::Status::Converted:
        return ConvertDialog::tr("Converted");
    case ConvertResult::Status::Skipped:
        return ConvertDialog::tr("Skipped: already there");
    case ConvertResult::Status::TagsUpdated:
        return ConvertDialog::tr("Tags updated");
    case ConvertResult::Status::Cancelled:
        return ConvertDialog::tr("Cancelled");
    case ConvertResult::Status::SourceNotFound:
        return ConvertDialog::tr("File not found");
    case ConvertResult::Status::SourceNotAudio:
        return ConvertDialog::tr("Not a music file");
    case ConvertResult::Status::InvalidJob:
        return ConvertDialog::tr("Cannot convert: %1").arg(result.message);
    case ConvertResult::Status::EncoderMissing:
        return ConvertDialog::tr("ffmpeg was not found");
    case ConvertResult::Status::Failed:
        return ConvertDialog::tr("Failed: %1").arg(result.message);
    }
    return { };
}

}

// CD-OPTIONS: the options as the controls say them, written at the start of a run.
void ConvertDialog::Private::saveSettings()
{
    if (!settings)
        return;
    settings->beginGroup(QStringLiteral("convert"));
    settings->setValue(QStringLiteral("preset"), presetCombo->currentData().toString());
    settings->setValue(QStringLiteral("folder"), folderEdit->text().trimmed());
    settings->setValue(QStringLiteral("ifExists"), ifExistsCombo->currentData().toString());
    settings->setValue(QStringLiteral("copyTags"), copyTagsCheck->isChecked());
    settings->setValue(QStringLiteral("copyPictures"), copyPicturesCheck->isChecked());
    settings->setValue(QStringLiteral("parallel"), parallelSpin->value());
    settings->endGroup();
    settings->sync();
}

// Controls are locked while a batch runs; the Close button stays usable.
void ConvertDialog::Private::setRunning(bool on)
{
    const QList<QWidget *> controls { presetCombo, folderEdit, browseButton, ifExistsCombo,
        parallelSpin, copyTagsCheck, runButton };
    for (QWidget *control : controls)
        control->setEnabled(!on);
    copyPicturesCheck->setEnabled(!on && copyTagsCheck->isChecked());
    runButton->setEnabled(!on && !files.isEmpty());
    progressBar->setVisible(on);
}

// Connected once, in the constructor. The batch's signals arrive on this
// thread; the context object is the dialog, so they stop when it is destroyed.
void ConvertDialog::Private::connectBatch()
{
    QObject::connect(batch, &ConvertBatch::jobStarted, q,
        [this](int index) { jobTable->topLevelItem(index)->setText(2, QStringLiteral("0%")); });
    QObject::connect(batch, &ConvertBatch::jobProgress, q, [this](int index, double fraction) {
        jobTable->topLevelItem(index)->setText(
            2, QStringLiteral("%1%").arg(qRound(fraction * 100)));
    });
    QObject::connect(
        batch, &ConvertBatch::jobFinished, q, [this](int index, const ConvertResult &result) {
            jobTable->topLevelItem(index)->setText(2, statusText(result));
            progressBar->setValue(progressBar->value() + 1);
        });
    QObject::connect(batch, &ConvertBatch::finished, q, [this] {
        int convertedCount = 0;
        int skippedCount = 0;
        int failedCount = 0;
        int cancelledCount = 0;
        QStringList outputs;
        for (const ConvertResult &result : batch->results()) {
            switch (result.status) {
            case ConvertResult::Status::Converted:
            case ConvertResult::Status::TagsUpdated:
                ++convertedCount;
                outputs.append(result.outputPath);
                break;
            case ConvertResult::Status::Skipped:
                ++skippedCount;
                break;
            case ConvertResult::Status::Cancelled:
                ++cancelledCount;
                break;
            default:
                ++failedCount;
                break;
            }
        }
        QString summary = ConvertDialog::tr("%1 converted, %2 skipped, %3 failed")
                              .arg(convertedCount)
                              .arg(skippedCount)
                              .arg(failedCount);
        if (cancelledCount > 0)
            summary += ConvertDialog::tr(", %1 cancelled").arg(cancelledCount);
        summaryLabel->setText(summary);
        setRunning(false);
        emit q->converted(outputs);
        // Nothing failed and nothing was cancelled: the dialog is done.
        if (failedCount == 0 && cancelledCount == 0)
            q->accept();
    });
}

// CD-RUN. Returns at once; the work goes on in the batch.
bool ConvertDialog::run()
{
    if (d->files.isEmpty() || d->batch->isRunning())
        return false;

    // CD-RUN 2: the options are stored now, and only now.
    d->saveSettings();
    const BatchOptions chosen = options();
    if (!chosen.outputFolder.isEmpty())
        QDir().mkpath(chosen.outputFolder);
    d->batch->setMaxParallel(d->parallelSpin->value());

    const QList<ConvertJob> jobs = plan();
    d->setRunning(true);
    d->progressBar->setRange(0, static_cast<int>(jobs.size()));
    d->progressBar->setValue(0);
    for (int row = 0; row < d->jobTable->topLevelItemCount(); ++row)
        d->jobTable->topLevelItem(row)->setText(2, tr("Waiting"));
    if (!d->batch->start(jobs)) {
        d->setRunning(false);
        return false;
    }
    return true;
}

}
