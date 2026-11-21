#pragma once
#include "preferences/librarypage.h"

class QSettings;

// Puts the Library page into the preferences and tells the background
// shell when it stored something. Contract:
// docs/spec/library-preferences.md, LP-INSTALL.
namespace linuxbee::ui {

class BackgroundShell;
class ToolsShell;

// Nothing is owned; all three must outlive every use of the preferences.
// `chooseFolder` is handed to each page (empty: the desktop's chooser).
// `searchStored`, when given, is called after the background shell is told (LP-INSTALL).
void installLibraryPreferences(ToolsShell *tools, BackgroundShell *background, QSettings *settings,
    std::function<QString(QWidget *parent)> chooseFolder = { },
    std::function<void()> searchStored = { });

}
