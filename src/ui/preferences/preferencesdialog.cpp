#include "preferences/preferencesdialog.h"

// Stub. Plan 10b replaces every body in this file.
namespace linuxbee::ui {

struct PreferencesDialog::Private { };

PreferencesDialog::PreferencesDialog(QWidget *parent)
    : QDialog(parent)
    , d(nullptr)
{
}

PreferencesDialog::~PreferencesDialog()
{
    Q_UNUSED(d);
}

void PreferencesDialog::addPage(PreferencesPage *page)
{
    page->setParent(this);
}

QStringList PreferencesDialog::pages() const
{
    return { };
}

QString PreferencesDialog::currentPage() const
{
    return { };
}

bool PreferencesDialog::selectPage(const QString &title)
{
    Q_UNUSED(title);
    return false;
}

bool PreferencesDialog::apply()
{
    return false;
}

void PreferencesDialog::reject()
{
    QDialog::reject();
}

}
