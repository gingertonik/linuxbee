#include "preferences/librarypage.h"

// Stub. Plan 34 replaces every body in this file.
namespace linuxbee::ui {

struct LibraryPage::Private { };

LibraryPage::LibraryPage(QSettings *settings, Hooks hooks, QWidget *parent)
    : PreferencesPage(parent)
    , d(nullptr)
{
    Q_UNUSED(settings);
    Q_UNUSED(hooks);
}

LibraryPage::~LibraryPage()
{
    Q_UNUSED(d);
}

QString LibraryPage::title() const
{
    return { };
}

void LibraryPage::load()
{
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
    return { };
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
