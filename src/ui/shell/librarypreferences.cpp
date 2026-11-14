#include "shell/librarypreferences.h"

#include <QSettings>

#include "preferences/librarypage.h"
#include "shell/backgroundshell.h"
#include "shell/toolsshell.h"

namespace linuxbee::ui {

void installLibraryPreferences(ToolsShell *tools, BackgroundShell *background, QSettings *settings,
    std::function<QString(QWidget *parent)> chooseFolder)
{
    // LP-INSTALL: each page made is over these settings, has this chooser, and
    // tells the background shell when it stored something (BG-RELOAD).
    tools->addPreferencesPage([background, settings, chooseFolder]() -> PreferencesPage * {
        LibraryPage::Hooks hooks;
        hooks.chooseFolder = chooseFolder;
        hooks.stored = [background] { background->reload(); };
        return new LibraryPage(settings, hooks);
    });
}

}
