#pragma once
#include <QString>
#include <QStringList>

#include <functional>

#include "preferences/preferencesdialog.h"

class QSettings;

// The Library page of the preferences: which folders are monitored, which
// section their new files go to, and whether they are checked at start-up.
// Contract: docs/spec/library-preferences.md (LP-x). The page only edits
// the settings; what they mean is in background-shell.md.
namespace linuxbee::ui {

class LibraryPage : public PreferencesPage {
    Q_OBJECT
public:
    struct Hooks {
        // Asks the user for a folder; empty when they gave up. By default
        // the desktop's folder chooser.
        std::function<QString(QWidget *parent)> chooseFolder;
        // Called after apply() has stored something.
        std::function<void()> stored;
    };

    // The settings are not owned and must outlive the page.
    explicit LibraryPage(QSettings *settings, Hooks hooks = { }, QWidget *parent = nullptr);
    ~LibraryPage() override;

    QString title() const override;
    void load() override;
    QString apply() override;
    bool isModified() const override;

    QStringList folders() const; // as the page shows them now
    bool addFolder(const QString &folder); // LP-ADD; whether it was added
    bool removeFolder(const QString &folder); // LP-REMOVE; whether it was there

private:
    struct Private;
    Private *d;
};

}
