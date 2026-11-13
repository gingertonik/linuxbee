#include <QLabel>
#include <QSettings>

#include "preferences/hotkeyspage.h"
#include "preferences/hotkeyspage_p.h"

// The edits of the Hotkeys page: the page keeps its pending keys to itself
// and nothing reaches the registry outside apply().
// Contract: docs/spec/preferences.md (HK-EDIT, HK-CONFLICT, HK-RESET, HK-APPLY).
namespace linuxbee::ui {

bool HotkeysPage::isModified() const
{
    for (auto it = d->pending.constBegin(); it != d->pending.constEnd(); ++it)
        if (d->actions->shortcut(it.key()) != it.value())
            return true;
    return false;
}

void HotkeysPage::checkModified()
{
    // HK-EDIT: modifiedChanged when the answer becomes true or false, not
    // at every edit. Remember the last answer.
    const bool now = isModified();
    if (now != d->reportedModified) {
        d->reportedModified = now;
        emit modifiedChanged();
    }
}

void HotkeysPage::load()
{
    for (auto it = d->pending.begin(); it != d->pending.end(); ++it)
        it.value() = d->actions->shortcut(it.key());
    refreshHotkeyCells();
    d->conflictLabel->clear();
    applyFilter();
    checkModified();
}

QKeySequence HotkeysPage::pending(const QString &id) const
{
    return d->pending.value(id); // empty for an unknown id
}

ShortcutResult HotkeysPage::assign(const QString &id, const QKeySequence &sequence)
{
    Q_UNUSED(id);
    Q_UNUSED(sequence);
    return { }; // Task 5: HK-EDIT
}

void HotkeysPage::reset(const QString &id)
{
    Q_UNUSED(id); // Task 5: HK-RESET
}

void HotkeysPage::resetAll()
{
    // Task 5: HK-RESET
}

QString HotkeysPage::apply()
{
    return { }; // Task 6: HK-APPLY
}

}
