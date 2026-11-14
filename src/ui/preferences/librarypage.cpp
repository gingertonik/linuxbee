#include "preferences/librarypage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

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

}

struct LibraryPage::Private {
    QSettings *settings = nullptr;
    QListWidget *monitoredList = nullptr;
    QPushButton *addFolderButton = nullptr;
    QPushButton *removeFolderButton = nullptr;
    QComboBox *sectionCombo = nullptr;
    QCheckBox *checkOnStartupBox = nullptr;
};

LibraryPage::LibraryPage(QSettings *settings, Hooks hooks, QWidget *parent)
    : PreferencesPage(parent)
    , d(new Private)
{
    Q_UNUSED(hooks);
    d->settings = settings;

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
    d->monitoredList->clear();
    d->monitoredList->addItems(storedFolders(d->settings));
    d->sectionCombo->setCurrentText(storedSection(d->settings));
    d->checkOnStartupBox->setChecked(storedCheckOnStartup(d->settings));
}

QString LibraryPage::apply()
{
    return { };
}

bool LibraryPage::isModified() const
{
    return false;
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
    Q_UNUSED(folder);
    return false;
}

bool LibraryPage::removeFolder(const QString &folder)
{
    Q_UNUSED(folder);
    return false;
}

}
