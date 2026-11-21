#include <QKeySequenceEdit>
#include <QLabel>
#include <QMessageBox>
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
    d->conflictIcon->hide(); // HK-BUTTONS: load() forgets the warning with the edits
    applyFilter();
    checkModified();
}

QKeySequence HotkeysPage::pending(const QString &id) const
{
    return d->pending.value(id); // empty for an unknown id
}

QString HotkeysPage::holderOf(const QString &id, const QKeySequence &sequence) const
{
    // HK-CONFLICT: the command, other than this one, that has the key among the pending keys.
    if (sequence.isEmpty() || sequence.count() > 1)
        return { };
    for (auto it = d->pending.constBegin(); it != d->pending.constEnd(); ++it)
        if (it.key() != id && it.value() == sequence)
            return it.key();
    return { };
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

void HotkeysPage::setConfirmMove(
    std::function<bool(const QKeySequence &key, const QString &holder)> confirm)
{
    d->confirmMove = std::move(confirm);
}

ShortcutResult HotkeysPage::move(const QString &id, const QKeySequence &sequence)
{
    // HK-MOVE: the same refusals as assign() for an unknown id and more than one combination.
    if (!d->pending.contains(id) || sequence.count() > 1)
        return assign(id, sequence);

    const QString holder = holderOf(id, sequence);
    if (holder.isEmpty()) { // nobody else has it: as assign()
        const ShortcutResult result = assign(id, sequence);
        if (result.status == ShortcutResult::Status::Ok)
            d->conflictIcon->hide();
        return result;
    }

    d->pending[holder] = QKeySequence(); // the holder is left with no key...
    d->pending[id] = sequence; // ...and this command has it
    setRowKey(rowForId(holder), QKeySequence());
    setRowKey(rowForId(id), sequence);
    applyFilter();
    d->conflictLabel->setText(sequence.toString(QKeySequence::NativeText) + tr(" moved from: ")
        + commandLabel(d->actions->info(holder)));
    d->conflictIcon->hide();
    checkModified();
    return { ShortcutResult::Status::Ok, holder };
}

void HotkeysPage::assignChosen()
{
    // HK-BUTTONS: Assign asks first when another command has the key.
    const QString id = selectedId();
    if (id.isEmpty())
        return;
    const QKeySequence sequence = d->keyEdit->keySequence();
    const QString holder = holderOf(id, sequence);
    if (holder.isEmpty()) {
        assign(id, sequence);
        return;
    }
    bool yes = false;
    if (d->confirmMove) {
        yes = d->confirmMove(sequence, holder);
    } else {
        yes = QMessageBox::question(this, tr("Hotkeys"),
                  sequence.toString(QKeySequence::NativeText) + tr(" is used by: ")
                      + commandLabel(d->actions->info(holder)) + tr("\n\nMove it to ")
                      + commandLabel(d->actions->info(id)) + tr("?"),
                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            == QMessageBox::Yes;
    }
    if (yes)
        move(id, sequence);
    else
        assign(id, sequence); // refuses, and the label says who has the key
}

QString HotkeysPage::apply()
{
    // HK-APPLY: the end state is exactly the pending keys. Take the key
    // away from every command that changed, then give each its new one,
    // so that commands which swapped keys cannot block each other.
    QStringList changed;
    for (auto it = d->pending.constBegin(); it != d->pending.constEnd(); ++it)
        if (d->actions->shortcut(it.key()) != it.value())
            changed.append(it.key());
    for (const QString &id : changed)
        d->actions->setShortcut(id, QKeySequence());
    for (const QString &id : changed) {
        if (d->actions->setShortcut(id, d->pending.value(id)).status != ShortcutResult::Status::Ok)
            return tr("The hotkey for %1 was refused by the registry.")
                .arg(commandLabel(d->actions->info(id)));
    }
    if (d->settings)
        d->actions->save(*d->settings); // H-STORE: the next start has them
    checkModified();
    return { };
}

}
