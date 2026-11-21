#include "shell/librarypreferences.h"

#include <QSettings>

#include "preferences/librarypage.h"
#include "shell/backgroundshell.h"
#include "shell/toolsshell.h"

namespace linuxbee::ui {

void installLibraryPreferences(ToolsShell *tools, BackgroundShell *background, QSettings *settings,
    std::function<QString(QWidget *parent)> chooseFolder, std::function<void()> searchStored)
{
    // LP-INSTALL: each page made is over these settings, has this chooser, and
    // tells the background shell when it stored something (BG-RELOAD), then the search hook.
    tools->addPreferencesPage(
        [background, settings, chooseFolder, searchStored]() -> PreferencesPage * {
            LibraryPage::Hooks hooks;
            hooks.chooseFolder = chooseFolder;
            hooks.stored = [background, searchStored] {
                background->reload();
                if (searchStored)
                    searchStored();
            };
            return new LibraryPage(settings, hooks);
        });
}

}
