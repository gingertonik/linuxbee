#include "actions/actionregistry.h"

// Stub. Plan 10 replaces every body in this file.
namespace linuxbee::ui {

struct ActionRegistry::Private { };

ActionRegistry::ActionRegistry(QObject *parent)
    : QObject(parent)
    , d(nullptr)
{
}

ActionRegistry::~ActionRegistry() = default;

QStringList ActionRegistry::ids() const
{
    return { };
}

QAction *ActionRegistry::action(const QString &id) const
{
    Q_UNUSED(id);
    return nullptr;
}

ActionInfo ActionRegistry::info(const QString &id) const
{
    Q_UNUSED(id);
    return { };
}

bool ActionRegistry::implement(const QString &id, QObject *context, std::function<void()> slot)
{
    Q_UNUSED(id);
    Q_UNUSED(context);
    Q_UNUSED(slot);
    return false;
}

bool ActionRegistry::isImplemented(const QString &id) const
{
    Q_UNUSED(id);
    return false;
}

QKeySequence ActionRegistry::shortcut(const QString &id) const
{
    Q_UNUSED(id);
    return { };
}

QKeySequence ActionRegistry::defaultShortcut(const QString &id) const
{
    Q_UNUSED(id);
    return { };
}

ShortcutResult ActionRegistry::setShortcut(const QString &id, const QKeySequence &sequence)
{
    Q_UNUSED(id);
    Q_UNUSED(sequence);
    return { ShortcutResult::Status::UnknownAction, { } };
}

void ActionRegistry::resetShortcut(const QString &id)
{
    Q_UNUSED(id);
}

void ActionRegistry::resetAllShortcuts()
{
}

void ActionRegistry::save(QSettings &settings) const
{
    Q_UNUSED(settings);
}

void ActionRegistry::load(QSettings &settings)
{
    Q_UNUSED(settings);
    Q_UNUSED(d);
}

void ActionRegistry::populateMenuBar(QMenuBar *menuBar) const
{
    Q_UNUSED(menuBar);
}

void ActionRegistry::populateMenu(QMenu *menu, const QString &menuPath) const
{
    Q_UNUSED(menu);
    Q_UNUSED(menuPath);
}

}
