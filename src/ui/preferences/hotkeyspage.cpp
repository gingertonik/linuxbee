#include "preferences/hotkeyspage.h"

// Stub. Plan 10b replaces every body in this file.
namespace linuxbee::ui {

struct HotkeysPage::Private { };

HotkeysPage::HotkeysPage(ActionRegistry *actions, QSettings *settings, QWidget *parent)
    : PreferencesPage(parent)
    , d(nullptr)
{
    Q_UNUSED(actions);
    Q_UNUSED(settings);
}

HotkeysPage::~HotkeysPage()
{
    Q_UNUSED(d);
}

QString HotkeysPage::title() const
{
    return { };
}

void HotkeysPage::load()
{
}

QString HotkeysPage::apply()
{
    return { };
}

bool HotkeysPage::isModified() const
{
    return false;
}

QKeySequence HotkeysPage::pending(const QString &id) const
{
    Q_UNUSED(id);
    return { };
}

ShortcutResult HotkeysPage::assign(const QString &id, const QKeySequence &sequence)
{
    Q_UNUSED(id);
    Q_UNUSED(sequence);
    return { };
}

void HotkeysPage::reset(const QString &id)
{
    Q_UNUSED(id);
}

void HotkeysPage::resetAll()
{
}

}
