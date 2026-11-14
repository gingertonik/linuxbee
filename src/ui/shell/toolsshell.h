#pragma once
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

#include "library/library.h"

class QDialog;
class QSettings;

// Opens the dialogs from the window: the tag editor, the organiser, the
// converter and the preferences, each on the tracks selected, and brings
// the library up to date with what they did. Contract:
// docs/spec/tools-shell.md (TS-x). It owns no rule of its own.
namespace linuxbee::ui {

class ActionRegistry;
class LibraryShell;
class MainWindow;
class PreferencesPage;

class ToolsShell : public QObject {
    Q_OBJECT
public:
    // Shows a dialog and returns when the user is done with it. By default
    // QDialog::exec(). Tests hand in a function that drives the dialog.
    using Presenter = std::function<void(QDialog *dialog)>;

    // Nothing handed in is owned; all of it must outlive the shell.
    ToolsShell(MainWindow *window, ActionRegistry *actions, Library *library,
        LibraryShell *libraryShell, QSettings *settings, Presenter presenter = { },
        QObject *parent = nullptr);
    ~ToolsShell() override;

    // TS-PREFERENCES: other plans add their pages here. The function is called
    // each time the preferences open and returns a new page, which the
    // dialog then owns. Pages come after Hotkeys, in the order added.
    using PageFactory = std::function<PreferencesPage *()>;
    void addPreferencesPage(PageFactory factory);
    QStringList preferencesPages() const; // the titles the dialog would show now

    QString lastMessage() const; // TS-MESSAGE: what the status bar was last told

private:
    struct Private;
    Private *d;
};

}
