#include "shell/toolsshell.h"

// Stub. Plan 32 replaces every body in this file.
namespace linuxbee::ui {

struct ToolsShell::Private { };

ToolsShell::ToolsShell(MainWindow *window, ActionRegistry *actions, Library *library,
    LibraryShell *libraryShell, QSettings *settings, Presenter presenter, QObject *parent)
    : QObject(parent)
    , d(nullptr)
{
    Q_UNUSED(window);
    Q_UNUSED(actions);
    Q_UNUSED(library);
    Q_UNUSED(libraryShell);
    Q_UNUSED(settings);
    Q_UNUSED(presenter);
}

ToolsShell::~ToolsShell()
{
    Q_UNUSED(d);
}

void ToolsShell::addPreferencesPage(PageFactory factory)
{
    Q_UNUSED(factory);
}

QStringList ToolsShell::preferencesPages() const
{
    return { };
}

QString ToolsShell::lastMessage() const
{
    return { };
}

}
