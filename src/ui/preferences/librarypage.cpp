#include "preferences/librarypage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <utility>

#include "library/library.h"

namespace linuxbee::ui {

namespace {

// LP-LOAD: the folders as the settings hold them, tidied, in stored order.
// Left out: empty, not absolute, or listed already.
QStringList storedFolders(const QSettings *settings)
{
    QStringList out;
    const QStringList stored = settings->value(QStringLiteral("library/monitored")).toStringList();
    for (const QString &raw : stored) {
        if (raw.isEmpty() || !QDir::isAbsolutePath(raw))
            continue;
        const QString folder = QDir::cleanPath(raw);
        if (!out.contains(folder))
            out.append(folder);
    }
    return out;
}

// LP-LOAD: the section; Inbox when missing or not a section.
QString storedSection(const QSettings *settings)
{
    const QString category
        = settings->value(QStringLiteral("library/monitoredCategory")).toString();
    if (Library::categories().contains(category))
        return category;
    return QStringLiteral("Inbox");
}

// LP-LOAD: checked when the stored text is true or 1, in any case of letters.
bool storedCheckOnStartup(const QSettings *settings)
{
    const QString text
        = settings->value(QStringLiteral("library/checkOnStartup")).toString().toLower();
    return text == QStringLiteral("true") || text == QStringLiteral("1");
}

// LP-LOAD: checked unless the stored text is false or 0, in any case of letters; missing is
// checked.
bool storedIgnoreAccents(const QSettings *settings)
{
    const QString text
        = settings->value(QStringLiteral("library/searchIgnoresAccents")).toString().toLower();
    return !(text == QStringLiteral("false") || text == QStringLiteral("0"));
}

}

struct LibraryPage::Private {
    QSettings *settings = nullptr;
    Hooks hooks;
    LibraryPage *owner = nullptr;
    QListWidget *monitoredList = nullptr;
    QPushButton *addFolderButton = nullptr;
    QPushButton *removeFolderButton = nullptr;
    QComboBox *sectionCombo = nullptr;
    QCheckBox *checkOnStartupBox = nullptr;
    QCheckBox *ignoreAccentsBox = nullptr;

    // What load() showed: the baseline "modified" is measured against.
    QStringList shownFolders;
    QString shownSection;
    bool shownCheckOnStartup = false;
    bool shownIgnoreAccents = true;
    // The last answer of isModified() that modifiedChanged announced.
    bool announced = false;

    // LP-MODIFIED: announce a change of the answer, and only a change.
    void noticeChange()
    {
        const bool now = owner->isModified();
        if (now != announced) {
            announced = now;
            emit owner->modifiedChanged();
        }
    }
};

LibraryPage::LibraryPage(QSettings *settings, Hooks hooks, QWidget *parent)
    : PreferencesPage(parent)
    , d(new Private)
{
    d->settings = settings;
    d->hooks = std::move(hooks);
    d->owner = this;

    auto *layout = new QVBoxLayout(this);

    d->monitoredList = new QListWidget(this);
    d->monitoredList->setObjectName(QStringLiteral("monitoredList"));
    layout->addWidget(d->monitoredList);

    auto *buttons = new QHBoxLayout;
    d->addFolderButton = new QPushButton(tr("Add Folder..."), this);
    d->addFolderButton->setObjectName(QStringLiteral("addFolderButton"));
    d->removeFolderButton = new QPushButton(tr("Remove Folder"), this);
    d->removeFolderButton->setObjectName(QStringLiteral("removeFolderButton"));
    d->removeFolderButton->setEnabled(false);
    buttons->addWidget(d->addFolderButton);
    buttons->addWidget(d->removeFolderButton);
    buttons->addStretch();
    layout->addLayout(buttons);

    auto *sectionRow = new QHBoxLayout;
    sectionRow->addWidget(new QLabel(tr("New files go to:"), this));
    d->sectionCombo = new QComboBox(this);
    d->sectionCombo->setObjectName(QStringLiteral("sectionCombo"));
    d->sectionCombo->setEditable(false);
    d->sectionCombo->addItems(Library::categories());
    sectionRow->addWidget(d->sectionCombo);
    sectionRow->addStretch();
    layout->addLayout(sectionRow);

    d->checkOnStartupBox = new QCheckBox(tr("Check for new files on start-up"), this);
    d->checkOnStartupBox->setObjectName(QStringLiteral("checkOnStartupBox"));
    layout->addWidget(d->checkOnStartupBox);

    // LP-STRUCTURE: the accent box, under the start-up box.
    d->ignoreAccentsBox = new QCheckBox(tr("Ignore accents when searching"), this);
    d->ignoreAccentsBox->setObjectName(QStringLiteral("ignoreAccentsBox"));
    layout->addWidget(d->ignoreAccentsBox);

    // LP-REMOVE: the remove button is enabled only while a row is the current one.
    connect(d->monitoredList, &QListWidget::currentRowChanged, this,
        [this](int row) { d->removeFolderButton->setEnabled(row >= 0); });
    // LP-ADD: the chooser answers a folder, or nothing when the user gave up.
    connect(d->addFolderButton, &QPushButton::clicked, this, [this] {
        const QString folder = d->hooks.chooseFolder
            ? d->hooks.chooseFolder(this)
            : QFileDialog::getExistingDirectory(this, tr("Monitored Folder"));
        if (!folder.isEmpty())
            addFolder(folder);
    });
    connect(d->removeFolderButton, &QPushButton::clicked, this, [this] {
        delete d->monitoredList->takeItem(d->monitoredList->currentRow());
        d->noticeChange();
    });
    connect(d->sectionCombo, &QComboBox::currentTextChanged, this,
        [this](const QString &) { d->noticeChange(); });
    connect(d->checkOnStartupBox, &QCheckBox::toggled, this, [this](bool) { d->noticeChange(); });
    connect(d->ignoreAccentsBox, &QCheckBox::toggled, this, [this](bool) { d->noticeChange(); });

    load();
}

LibraryPage::~LibraryPage()
{
    delete d;
}

QString LibraryPage::title() const
{
    return tr("Library");
}

void LibraryPage::load()
{
    // The widgets are filled without announcing each step; one answer follows.
    {
        QSignalBlocker a(d->monitoredList);
        QSignalBlocker b(d->sectionCombo);
        QSignalBlocker c(d->checkOnStartupBox);
        QSignalBlocker e(d->ignoreAccentsBox);
        d->monitoredList->clear();
        d->shownFolders = storedFolders(d->settings);
        d->monitoredList->addItems(d->shownFolders);
        d->shownSection = storedSection(d->settings);
        d->sectionCombo->setCurrentText(d->shownSection);
        d->shownCheckOnStartup = storedCheckOnStartup(d->settings);
        d->checkOnStartupBox->setChecked(d->shownCheckOnStartup);
        d->shownIgnoreAccents = storedIgnoreAccents(d->settings);
        d->ignoreAccentsBox->setChecked(d->shownIgnoreAccents);
    }
    d->removeFolderButton->setEnabled(d->monitoredList->currentRow() >= 0);
    d->noticeChange();
}

QString LibraryPage::apply()
{
    // LP-APPLY: never refuses; nothing to store means nothing happens.
    if (!isModified())
        return { };
    const QString section = d->sectionCombo->currentText();
    const bool checked = d->checkOnStartupBox->isChecked();
    const bool ignore = d->ignoreAccentsBox->isChecked();
    d->settings->setValue(QStringLiteral("library/monitored"), folders());
    d->settings->setValue(QStringLiteral("library/monitoredCategory"), section);
    d->settings->setValue(QStringLiteral("library/checkOnStartup"), checked);
    d->settings->setValue(QStringLiteral("library/searchIgnoresAccents"), ignore);
    d->shownFolders = folders();
    d->shownSection = section;
    d->shownCheckOnStartup = checked;
    d->shownIgnoreAccents = ignore;
    d->noticeChange();
    if (d->hooks.stored)
        d->hooks.stored();
    return { };
}

bool LibraryPage::isModified() const
{
    return folders() != d->shownFolders || d->sectionCombo->currentText() != d->shownSection
        || d->checkOnStartupBox->isChecked() != d->shownCheckOnStartup
        || d->ignoreAccentsBox->isChecked() != d->shownIgnoreAccents;
}

QStringList LibraryPage::folders() const
{
    QStringList out;
    for (int i = 0; i < d->monitoredList->count(); ++i)
        out.append(d->monitoredList->item(i)->text());
    return out;
}

bool LibraryPage::addFolder(const QString &folder)
{
    // LP-ADD: empty or not absolute is refused; listed already selects that row.
    if (folder.isEmpty() || !QDir::isAbsolutePath(folder))
        return false;
    const QString tidy = QDir::cleanPath(folder);
    for (int row = 0; row < d->monitoredList->count(); ++row) {
        if (d->monitoredList->item(row)->text() == tidy) {
            d->monitoredList->setCurrentRow(row);
            return false;
        }
    }
    d->monitoredList->addItem(tidy);
    d->monitoredList->setCurrentRow(d->monitoredList->count() - 1);
    d->noticeChange();
    return true;
}

bool LibraryPage::removeFolder(const QString &folder)
{
    // LP-REMOVE: compared tidied; nothing is listed under an empty name.
    if (folder.isEmpty())
        return false;
    const QString tidy = QDir::cleanPath(folder);
    for (int row = 0; row < d->monitoredList->count(); ++row) {
        if (d->monitoredList->item(row)->text() == tidy) {
            delete d->monitoredList->takeItem(row);
            d->noticeChange();
            return true;
        }
    }
    return false;
}

}
