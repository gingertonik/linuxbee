#include "convert/convertdialog_p.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace linuxbee::ui {

// CD-OPTIONS: the stored values, or the defaults for anything missing or
// out of range. Nothing is written here.
void ConvertDialog::Private::loadSettings()
{
    if (!settings)
        return;
    settings->beginGroup(QStringLiteral("convert"));
    const int preset = presetCombo->findData(settings->value(QStringLiteral("preset")).toString());
    presetCombo->setCurrentIndex(preset < 0 ? 0 : preset);
    folderEdit->setText(settings->value(QStringLiteral("folder")).toString());
    const int ifExists
        = ifExistsCombo->findData(settings->value(QStringLiteral("ifExists")).toString());
    ifExistsCombo->setCurrentIndex(ifExists < 0 ? 0 : ifExists);
    copyTagsCheck->setChecked(settings->value(QStringLiteral("copyTags"), true).toBool());
    copyPicturesCheck->setChecked(settings->value(QStringLiteral("copyPictures"), true).toBool());
    parallelSpin->setValue(std::clamp(
        settings->value(QStringLiteral("parallel"), ConvertBatch::defaultParallel()).toInt(), 1,
        16));
    settings->endGroup();
}

// CD-TABLE: the table is planBatch of the files and the current options.
void ConvertDialog::Private::refreshTable()
{
    const QList<ConvertJob> jobs = planBatch(files, q->options());
    jobTable->clear();
    for (const ConvertJob &job : jobs)
        new QTreeWidgetItem(jobTable, QStringList { job.source, job.destination, QString() });
    const int count = files.size();
    summaryLabel->setText(
        count == 1 ? ConvertDialog::tr("1 file") : ConvertDialog::tr("%1 files").arg(count));
    runButton->setEnabled(count > 0);
}

ConvertDialog::ConvertDialog(const QStringList &files, QSettings *settings, QWidget *parent)
    : QDialog(parent)
    , d(new Private)
{
    d->q = this;
    d->files = files;
    d->settings = settings;
    d->batch = new ConvertBatch(this);

    setObjectName(QStringLiteral("convertDialog"));
    setWindowTitle(tr("Convert Format"));

    d->presetCombo = new QComboBox(this);
    d->presetCombo->setObjectName(QStringLiteral("presetCombo"));
    for (const EncoderPreset &preset : encoderPresets())
        d->presetCombo->addItem(preset.name, preset.id);

    d->folderEdit = new QLineEdit(this);
    d->folderEdit->setObjectName(QStringLiteral("folderEdit"));
    d->folderEdit->setPlaceholderText(tr("Next to the originals"));

    d->browseButton = new QPushButton(tr("Browse..."), this);
    d->browseButton->setObjectName(QStringLiteral("browseButton"));

    d->ifExistsCombo = new QComboBox(this);
    d->ifExistsCombo->setObjectName(QStringLiteral("ifExistsCombo"));
    d->ifExistsCombo->addItem(tr("Skip the file"), QStringLiteral("skip"));
    d->ifExistsCombo->addItem(tr("Overwrite it"), QStringLiteral("overwrite"));
    d->ifExistsCombo->addItem(tr("Keep both (rename the new file)"), QStringLiteral("rename"));
    d->ifExistsCombo->addItem(tr("Update its tags only"), QStringLiteral("updateTags"));

    d->parallelSpin = new QSpinBox(this);
    d->parallelSpin->setObjectName(QStringLiteral("parallelSpin"));
    d->parallelSpin->setRange(1, 16);
    d->parallelSpin->setValue(ConvertBatch::defaultParallel());

    d->copyTagsCheck = new QCheckBox(tr("Copy tags"), this);
    d->copyTagsCheck->setObjectName(QStringLiteral("copyTagsCheck"));
    d->copyPicturesCheck = new QCheckBox(tr("Copy artwork"), this);
    d->copyPicturesCheck->setObjectName(QStringLiteral("copyPicturesCheck"));

    d->jobTable = new QTreeWidget(this);
    d->jobTable->setObjectName(QStringLiteral("jobTable"));
    d->jobTable->setColumnCount(3);
    d->jobTable->setHeaderLabels(QStringList { tr("From"), tr("To"), tr("Status") });
    d->jobTable->setRootIsDecorated(false);

    d->summaryLabel = new QLabel(this);
    d->summaryLabel->setObjectName(QStringLiteral("summaryLabel"));

    d->progressBar = new QProgressBar(this);
    d->progressBar->setObjectName(QStringLiteral("progressBar"));
    d->progressBar->setVisible(false);

    d->runButton = new QPushButton(tr("Convert"), this);
    d->runButton->setObjectName(QStringLiteral("runButton"));
    d->closeButton = new QPushButton(tr("Close"), this);
    d->closeButton->setObjectName(QStringLiteral("closeButton"));
    auto *buttons = new QDialogButtonBox(this);
    buttons->addButton(d->runButton, QDialogButtonBox::ActionRole);
    buttons->addButton(d->closeButton, QDialogButtonBox::ActionRole);

    auto *form = new QGridLayout;
    form->addWidget(new QLabel(tr("Format"), this), 0, 0);
    form->addWidget(d->presetCombo, 0, 1, 1, 2);
    form->addWidget(new QLabel(tr("Folder"), this), 1, 0);
    form->addWidget(d->folderEdit, 1, 1);
    form->addWidget(d->browseButton, 1, 2);
    form->addWidget(new QLabel(tr("If the file is already there"), this), 2, 0);
    form->addWidget(d->ifExistsCombo, 2, 1, 1, 2);
    form->addWidget(new QLabel(tr("Files at a time"), this), 3, 0);
    form->addWidget(d->parallelSpin, 3, 1, 1, 2);
    form->addWidget(d->copyTagsCheck, 4, 0, 1, 3);
    form->addWidget(d->copyPicturesCheck, 5, 0, 1, 3);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(d->jobTable, 1);
    layout->addWidget(d->summaryLabel);
    layout->addWidget(d->progressBar);
    layout->addWidget(buttons);

    d->loadSettings();
    d->copyPicturesCheck->setEnabled(d->copyTagsCheck->isChecked());

    connect(d->presetCombo, &QComboBox::currentIndexChanged, this, [this] { d->refreshTable(); });
    connect(d->folderEdit, &QLineEdit::textChanged, this, [this] { d->refreshTable(); });
    connect(d->ifExistsCombo, &QComboBox::currentIndexChanged, this, [this] { d->refreshTable(); });
    connect(d->copyTagsCheck, &QCheckBox::toggled, this, [this](bool on) {
        d->copyPicturesCheck->setEnabled(on);
        d->refreshTable();
    });
    connect(d->copyPicturesCheck, &QCheckBox::toggled, this, [this] { d->refreshTable(); });
    connect(d->browseButton, &QPushButton::clicked, this, [this] {
        const QString picked = QFileDialog::getExistingDirectory(
            this, tr("Choose a folder for the results"), d->folderEdit->text());
        if (!picked.isEmpty())
            d->folderEdit->setText(picked);
    });
    connect(d->runButton, &QPushButton::clicked, this, [this] { run(); });
    connect(d->closeButton, &QPushButton::clicked, this, [this] { reject(); });

    d->connectBatch();
    d->refreshTable();
}

ConvertDialog::~ConvertDialog()
{
    // The batch goes first: its destructor waits for any job still working,
    // so no result reaches a widget after the widgets are freed.
    delete d->batch;
    d->batch = nullptr;
    delete d;
}

// CD-OPTIONS: what the controls say now.
BatchOptions ConvertDialog::options() const
{
    BatchOptions o;
    o.outputFolder = d->folderEdit->text().trimmed();
    const QList<EncoderPreset> presets = encoderPresets();
    o.settings = presets.value(std::max(0, d->presetCombo->currentIndex())).settings;

    const QString ifExists = d->ifExistsCombo->currentData().toString();
    if (ifExists == QStringLiteral("overwrite"))
        o.ifExists = ConvertJob::IfExists::Overwrite;
    else if (ifExists == QStringLiteral("rename"))
        o.ifExists = ConvertJob::IfExists::Rename;
    else if (ifExists == QStringLiteral("updateTags"))
        o.ifExists = ConvertJob::IfExists::UpdateTags;
    else
        o.ifExists = ConvertJob::IfExists::Skip;

    o.copyTags = d->copyTagsCheck->isChecked();
    o.copyPictures = o.copyTags && d->copyPicturesCheck->isChecked();
    return o;
}

QList<ConvertJob> ConvertDialog::plan() const
{
    return planBatch(d->files, options());
}

ConvertBatch *ConvertDialog::batch() const
{
    return d->batch;
}

void ConvertDialog::reject()
{
    QDialog::reject();
}

}
