#include "organise/organisedialog_p.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace linuxbee::ui {

namespace {

// OD-OPTIONS: the organiser's default naming template.
QString defaultTemplate()
{
    return QStringLiteral("<Album Artist>\\<Album>\\<Disc-Track#> <Title>");
}

}

OrganiseDialog::OrganiseDialog(const QStringList &files, QSettings *settings, QWidget *parent)
    : QDialog(parent)
    , d(new Private)
{
    setObjectName(QStringLiteral("organiseDialog"));
    setWindowTitle(tr("Organise Files"));
    d->files = files;
    d->settings = settings;
    d->planner = &linuxbee::planOrganise;
    d->executor = &linuxbee::executeOrganise;

    // OD-FRAME: the controls.
    auto *targetLabel = new QLabel(tr("Target folder:"), this);
    d->targetEdit = new QLineEdit(this);
    d->targetEdit->setObjectName(QStringLiteral("targetEdit"));
    d->targetEdit->setPlaceholderText(tr("Empty: each file stays in its folder"));
    d->browseButton = new QPushButton(tr("Browse..."), this);
    d->browseButton->setObjectName(QStringLiteral("browseButton"));

    auto *templateLabel = new QLabel(tr("Naming template:"), this);
    d->templateEdit = new QLineEdit(this);
    d->templateEdit->setObjectName(QStringLiteral("templateEdit"));

    d->problemLabel = new QLabel(this);
    d->problemLabel->setObjectName(QStringLiteral("problemLabel"));

    d->moveRadio = new QRadioButton(tr("Move"), this);
    d->moveRadio->setObjectName(QStringLiteral("moveRadio"));
    d->copyRadio = new QRadioButton(tr("Copy"), this);
    d->copyRadio->setObjectName(QStringLiteral("copyRadio"));

    d->replaceCheck = new QCheckBox(tr("Replace files that are already there"), this);
    d->replaceCheck->setObjectName(QStringLiteral("replaceCheck"));
    d->otherFilesCheck = new QCheckBox(tr("Move the folder's other files along (move only)"), this);
    d->otherFilesCheck->setObjectName(QStringLiteral("otherFilesCheck"));
    d->removeEmptyCheck = new QCheckBox(tr("Remove folders left empty (move only)"), this);
    d->removeEmptyCheck->setObjectName(QStringLiteral("removeEmptyCheck"));
    d->skipMissingCheck = new QCheckBox(tr("Leave files with missing tags where they are"), this);
    d->skipMissingCheck->setObjectName(QStringLiteral("skipMissingCheck"));

    auto *optionsBox = new QGroupBox(tr("Options"), this);
    auto *grid = new QGridLayout(optionsBox);
    grid->addWidget(targetLabel, 0, 0);
    grid->addWidget(d->targetEdit, 0, 1);
    grid->addWidget(d->browseButton, 0, 2);
    grid->addWidget(templateLabel, 1, 0);
    grid->addWidget(d->templateEdit, 1, 1, 1, 2);
    grid->addWidget(d->problemLabel, 2, 1, 1, 2);
    grid->addWidget(d->moveRadio, 3, 0);
    grid->addWidget(d->copyRadio, 3, 1, 1, 2);
    grid->addWidget(d->replaceCheck, 4, 0, 1, 3);
    grid->addWidget(d->otherFilesCheck, 5, 0, 1, 3);
    grid->addWidget(d->removeEmptyCheck, 6, 0, 1, 3);
    grid->addWidget(d->skipMissingCheck, 7, 0, 1, 3);

    d->previewTable = new QTreeWidget(this);
    d->previewTable->setObjectName(QStringLiteral("previewTable"));
    d->previewTable->setHeaderLabels({ tr("From"), tr("To"), tr("Status") });
    d->previewTable->setRootIsDecorated(false);

    d->summaryLabel = new QLabel(this);
    d->summaryLabel->setObjectName(QStringLiteral("summaryLabel"));

    d->progressBar = new QProgressBar(this);
    d->progressBar->setObjectName(QStringLiteral("progressBar"));
    d->progressBar->setVisible(false);

    d->runButton = new QPushButton(tr("Organise"), this);
    d->runButton->setObjectName(QStringLiteral("runButton"));
    d->closeButton = new QPushButton(tr("Close"), this);
    d->closeButton->setObjectName(QStringLiteral("closeButton"));
    d->buttons = new QDialogButtonBox(this);
    d->buttons->setObjectName(QStringLiteral("buttons"));
    d->buttons->addButton(d->runButton, QDialogButtonBox::ActionRole);
    d->buttons->addButton(d->closeButton, QDialogButtonBox::RejectRole);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(optionsBox);
    layout->addWidget(d->previewTable, 1);
    layout->addWidget(d->summaryLabel);
    layout->addWidget(d->progressBar);
    layout->addWidget(d->buttons);

    // OD-OPTIONS: the stored options, or the defaults when nothing is stored.
    auto stored = [this](const QString &key, const QVariant &fallback) {
        return d->settings ? d->settings->value(key, fallback) : fallback;
    };
    d->targetEdit->setText(stored(QStringLiteral("organise/target"), QString()).toString());
    d->templateEdit->setText(
        stored(QStringLiteral("organise/template"), defaultTemplate()).toString());
    const bool copy = stored(QStringLiteral("organise/copy"), false).toBool();
    d->copyRadio->setChecked(copy);
    d->moveRadio->setChecked(!copy);
    d->replaceCheck->setChecked(stored(QStringLiteral("organise/replace"), false).toBool());
    d->otherFilesCheck->setChecked(stored(QStringLiteral("organise/otherFiles"), false).toBool());
    d->removeEmptyCheck->setChecked(stored(QStringLiteral("organise/removeEmpty"), false).toBool());
    d->skipMissingCheck->setChecked(stored(QStringLiteral("organise/skipMissing"), true).toBool());

    // OD-OPTIONS: the two move-only boxes are greyed out while copying and keep their ticks.
    auto moveOnly = [this] {
        const bool move = d->moveRadio->isChecked();
        d->otherFilesCheck->setEnabled(move);
        d->removeEmptyCheck->setEnabled(move);
    };
    connect(d->moveRadio, &QRadioButton::toggled, this, moveOnly);
    moveOnly();

    connect(d->browseButton, &QPushButton::clicked, this, [this] {
        const QString chosen
            = QFileDialog::getExistingDirectory(this, tr("Organise into"), d->targetEdit->text());
        if (!chosen.isEmpty())
            d->targetEdit->setText(chosen);
    });
    // OD-RUN: the button and run() are the same action.
    connect(d->runButton, &QPushButton::clicked, this, [this] { run(); });
    // OD-RUN: during a run, Close asks the run to stop; otherwise it closes as rejected.
    connect(d->closeButton, &QPushButton::clicked, this, [this] {
        if (d->running)
            d->stopRequested = true;
        else
            reject();
    });

    // OD-PREVIEW: every control that changes what the planner is asked gets a fresh preview.
    connect(d->targetEdit, &QLineEdit::textChanged, this, [this] { refreshPreview(); });
    connect(d->templateEdit, &QLineEdit::textChanged, this, [this] { refreshPreview(); });
    for (QAbstractButton *b : { static_cast<QAbstractButton *>(d->moveRadio),
             static_cast<QAbstractButton *>(d->copyRadio) })
        connect(b, &QAbstractButton::toggled, this, [this] { refreshPreview(); });
    for (QCheckBox *c :
        { d->replaceCheck, d->otherFilesCheck, d->removeEmptyCheck, d->skipMissingCheck })
        connect(c, &QCheckBox::toggled, this, [this] { refreshPreview(); });
    refreshPreview();
}

OrganiseDialog::~OrganiseDialog()
{
    delete d;
}

void OrganiseDialog::setPlanner(Planner planner)
{
    d->planner = std::move(planner);
    refreshPreview();
}

void OrganiseDialog::setExecutor(Executor executor)
{
    d->executor = std::move(executor);
}

QString OrganiseDialog::templateText() const
{
    return d->templateEdit->text();
}

OrganiseOptions OrganiseDialog::options() const
{
    OrganiseOptions o;
    const QString target = d->targetEdit->text().trimmed();
    o.targetRoot = target.isEmpty() ? QString() : QDir::cleanPath(target);
    o.namingTemplate = Template::parse(templateText());
    o.pathRules.skipWhenTagsMissing = d->skipMissingCheck->isChecked();
    const bool copy = d->copyRadio->isChecked();
    o.mode = copy ? OrganiseOptions::Mode::Copy : OrganiseOptions::Mode::Move;
    o.replaceExisting = d->replaceCheck->isChecked();
    o.moveOtherFiles = !copy && d->otherFilesCheck->isChecked();
    o.removeEmptyFolders = !copy && d->removeEmptyCheck->isChecked();
    return o;
}

}
