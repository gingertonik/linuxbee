#include "actions/actionregistry.h"

#include <QAction>
#include <QEvent>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QWidget>

#include <algorithm>

// Plan 10: the command catalogue made live. Contract: docs/spec/main-window.md
// (H-CATALOGUE to H-STORE). The menu building (H-MENU) lives in actionmenus.cpp.
namespace linuxbee::ui {

struct ActionRegistry::Private {
    struct Entry {
        ActionInfo info;
        QAction *action = nullptr;
        QKeySequence def; // the catalogue's hotkey, parsed
        QObject *owner = nullptr; // H-IMPLEMENT: the object that gave this command its behaviour
    };
    QList<Entry> entries; // catalogue order
    QHash<QString, int> index; // id -> position in entries

    int indexOf(const QString &id) const { return index.value(id, -1); }
    static bool sameKeys(const QKeySequence &a, const QKeySequence &b)
    {
        // QKeySequence() == QKeySequence() is false in Qt; empty matches empty only by convention.
        if (a.isEmpty() || b.isEmpty())
            return a.isEmpty() && b.isEmpty();
        return a == b;
    }
    QKeySequence keyOf(const QString &id) const
    {
        const int i = indexOf(id);
        return i < 0 ? QKeySequence() : entries.at(i).action->shortcut();
    }
};

ActionRegistry::ActionRegistry(QObject *parent)
    : QObject(parent)
    , d(new Private)
{
    for (const ActionInfo &info : actionCatalogue()) {
        Private::Entry entry;
        entry.info = info;
        entry.def = QKeySequence(info.defaultShortcut, QKeySequence::PortableText);
        entry.action = new QAction(this);
        entry.action->setObjectName(info.id);
        entry.action->setText(info.text);
        entry.action->setCheckable(info.checkable);
        entry.action->setShortcut(entry.def);
        entry.action->setShortcutContext(Qt::WindowShortcut);
        entry.action->setEnabled(false); // H-IMPLEMENT: nothing works until something implements it
        d->entries.append(entry);
        d->index.insert(info.id, d->entries.size() - 1);
    }
}

ActionRegistry::~ActionRegistry()
{
    delete d;
}

QStringList ActionRegistry::ids() const
{
    QStringList out;
    for (const Private::Entry &entry : d->entries)
        out.append(entry.info.id);
    return out;
}

QAction *ActionRegistry::action(const QString &id) const
{
    const int i = d->indexOf(id);
    return i < 0 ? nullptr : d->entries.at(i).action;
}

ActionInfo ActionRegistry::info(const QString &id) const
{
    const int i = d->indexOf(id);
    return i < 0 ? ActionInfo() : d->entries.at(i).info;
}

bool ActionRegistry::implement(const QString &id, QObject *context, std::function<void()> slot)
{
    // H-IMPLEMENT: unknown id, null context, empty function, already done: refuse, change nothing.
    const int i = d->indexOf(id);
    if (i < 0 || !context || !slot)
        return false;
    Private::Entry &entry = d->entries[i];
    if (entry.owner)
        return false;
    connect(entry.action, &QAction::triggered, context, [slot] { slot(); });
    entry.action->setEnabled(true);
    entry.owner = context;
    // When the implementing object goes, the command is disabled and free again.
    connect(context, &QObject::destroyed, this, [this, i, context] {
        if (d->entries[i].owner == context) {
            d->entries[i].owner = nullptr;
            d->entries[i].action->setEnabled(false);
        }
    });
    return true;
}

bool ActionRegistry::isImplemented(const QString &id) const
{
    const int i = d->indexOf(id);
    return i >= 0 && d->entries.at(i).owner != nullptr;
}

QKeySequence ActionRegistry::shortcut(const QString &id) const
{
    return d->keyOf(id);
}

QKeySequence ActionRegistry::defaultShortcut(const QString &id) const
{
    const int i = d->indexOf(id);
    return i < 0 ? QKeySequence() : d->entries.at(i).def;
}

ShortcutResult ActionRegistry::setShortcut(const QString &id, const QKeySequence &sequence)
{
    // H-SET
    const int i = d->indexOf(id);
    if (i < 0)
        return { ShortcutResult::Status::UnknownAction, QString() };
    if (sequence.count() > 1)
        return { ShortcutResult::Status::Invalid, QString() };
    if (!sequence.isEmpty()) {
        for (const Private::Entry &entry : d->entries) {
            if (entry.info.id != id && entry.action->shortcut() == sequence)
                return { ShortcutResult::Status::Conflict, entry.info.id };
        }
    }
    QAction *act = d->entries[i].action;
    const bool changed = !Private::sameKeys(act->shortcut(), sequence);
    act->setShortcut(sequence);
    if (changed)
        emit shortcutChanged(id);
    return { ShortcutResult::Status::Ok, QString() };
}

void ActionRegistry::resetShortcut(const QString &id)
{
    // H-SET: the default if it is free, otherwise no hotkey at all.
    if (!d->index.contains(id))
        return;
    setShortcut(id, QKeySequence());
    const QKeySequence def = defaultShortcut(id);
    if (!def.isEmpty())
        setShortcut(id, def); // refused if another command holds it; then it stays empty
}

void ActionRegistry::resetAllShortcuts()
{
    // Every command first loses its hotkey, so no default collides with a command
    // that has not had its turn yet; then the defaults go back in catalogue order.
    for (const Private::Entry &entry : d->entries)
        setShortcut(entry.info.id, QKeySequence());
    for (const Private::Entry &entry : d->entries) {
        if (!entry.def.isEmpty())
            setShortcut(entry.info.id, entry.def);
    }
}

void ActionRegistry::save(QSettings &settings) const
{
    // H-STORE: the group `hotkeys` holds only the hotkeys that differ from the default.
    settings.beginGroup(QStringLiteral("hotkeys"));
    for (const QString &key : settings.childKeys())
        settings.remove(key); // rewrite the group: nothing outside it is touched
    for (const Private::Entry &entry : d->entries) {
        const QKeySequence current = entry.action->shortcut();
        if (Private::sameKeys(current, entry.def))
            continue; // defaults are not stored
        settings.setValue(entry.info.id, current.toString(QKeySequence::PortableText));
    }
    settings.endGroup();
}

void ActionRegistry::load(QSettings &settings)
{
    // H-STORE: start from the defaults, then apply the stored keys in catalogue order.
    resetAllShortcuts();
    settings.beginGroup(QStringLiteral("hotkeys"));
    const QStringList stored = settings.childKeys();
    settings.endGroup();
    for (const Private::Entry &entry :
        d->entries) { // catalogue order: of two claims, the earlier wins
        if (!stored.contains(entry.info.id))
            continue;
        settings.beginGroup(QStringLiteral("hotkeys"));
        const QString text = settings.value(entry.info.id).toString();
        settings.endGroup();
        setShortcut(entry.info.id, QKeySequence()); // it first loses its hotkey
        const QKeySequence seq(text, QKeySequence::PortableText);
        if (seq.isEmpty() || seq.count() != 1)
            continue; // unreadable, empty, a chord of keys: it ends with none
        setShortcut(entry.info.id, seq); // refused if another command holds it: it ends with none
    }
}

void ActionRegistry::populateMenuBar(QMenuBar *menuBar) const
{
    // H-MENU. Implemented in actionmenus.cpp (task 3).
    Q_UNUSED(menuBar);
}

void ActionRegistry::populateMenu(QMenu *menu, const QString &menuPath) const
{
    // H-MENU. Implemented in actionmenus.cpp (task 3).
    Q_UNUSED(menu);
    Q_UNUSED(menuPath);
}

} // namespace linuxbee::ui
