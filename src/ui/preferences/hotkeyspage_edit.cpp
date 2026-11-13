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
    if (!d->pending.contains(id))
        return { ShortcutResult::Status::UnknownAction, QString() };

    if (sequence.count() > 1) { // HK-EDIT: more than one combination
        d->conflictLabel->setText(tr("Only one key combination can be used."));
        return { ShortcutResult::Status::Invalid, QString() };
    }

    // HK-CONFLICT: judged against the pending keys, not against the registry.
    if (!sequence.isEmpty()) {
        for (auto it = d->pending.constBegin(); it != d->pending.constEnd(); ++it) {
            if (it.key() != id && it.value() == sequence) {
                d->conflictLabel->setText(sequence.toString(QKeySequence::NativeText)
                    + tr(" is used by: ") + commandLabel(d->actions->info(it.key())));
                return { ShortcutResult::Status::Conflict, it.key() };
            }
        }
    }

    d->pending[id] = sequence; // empty sequence: no hotkey
    setRowKey(rowForId(id), sequence);
    applyFilter(); // HK-FILTER: the cells, and so the filter, see the edit
    d->conflictLabel->clear();
    checkModified();
    return { };
}

void HotkeysPage::reset(const QString &id)
{
    if (!d->pending.contains(id))
        return; // HK-RESET: an unknown id does nothing

    const QKeySequence def = d->actions->defaultShortcut(id);
    QString holder;
    if (!def.isEmpty()) {
        for (auto it = d->pending.constBegin(); it != d->pending.constEnd(); ++it) {
            if (it.key() != id && it.value() == def) {
                holder = it.key();
                break;
            }
        }
    }
    if (holder.isEmpty())
        d->pending[id] = def;
    else
        d->pending[id] = QKeySequence(); // HK-RESET: left with no key...
    setRowKey(rowForId(id), d->pending.value(id));
    applyFilter();
    if (!holder.isEmpty()) // ...and the label says who has the default
        d->conflictLabel->setText(def.toString(QKeySequence::NativeText) + tr(" is used by: ")
            + commandLabel(d->actions->info(holder)));
    checkModified();
}

void HotkeysPage::resetAll()
{
    for (auto it = d->pending.begin(); it != d->pending.end(); ++it)
        it.value() = d->actions->defaultShortcut(it.key());
    refreshHotkeyCells();
    d->conflictLabel->clear();
    applyFilter();
    checkModified();
}

QString HotkeysPage::apply()
{
    return { }; // Task 6: HK-APPLY
}

}
