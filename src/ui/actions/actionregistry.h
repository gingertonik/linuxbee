#pragma once
#include <QKeySequence>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

class QAction;
class QMenu;
class QMenuBar;
class QSettings;

// Every command the application has, in one place: its text, where it sits
// in the menus, and its hotkey. Contract: docs/spec/main-window.md (H-x).
// The window, the context menus, the hotkey preferences and (later) the
// tray and the desktop integration all take their commands from here.
namespace linuxbee::ui {

struct ActionInfo {
    QString id; // "controls.playPause"
    QString text; // as shown in a menu
    QString menu; // "File", "Tools/Tagging Tools", "Context/Rating", or empty: hotkey only
    QString defaultShortcut; // portable text, e.g. "Ctrl+P"; empty: none
    bool checkable = false;
    bool separatorBefore = false;
};

// H-CATALOGUE: every command, in menu order. (actioncatalogue.cpp, locked.)
QList<ActionInfo> actionCatalogue();

struct ShortcutResult {
    enum class Status { Ok, UnknownAction, Invalid, Conflict };
    Status status = Status::Ok;
    QString conflictsWith; // Conflict: the id of the action that has the key
};

class ActionRegistry : public QObject {
    Q_OBJECT
public:
    explicit ActionRegistry(QObject *parent = nullptr);
    ~ActionRegistry() override;

    QStringList ids() const; // catalogue order
    QAction *action(const QString &id) const; // null for an unknown id
    ActionInfo info(const QString &id) const; // default-constructed for an unknown id

    // H-IMPLEMENT: gives a command its behaviour and enables it. False when
    // the id is unknown or the command has a behaviour already.
    bool implement(const QString &id, QObject *context, std::function<void()> slot);
    bool isImplemented(const QString &id) const;

    // H-SET
    QKeySequence shortcut(const QString &id) const;
    QKeySequence defaultShortcut(const QString &id) const;
    ShortcutResult setShortcut(const QString &id, const QKeySequence &sequence);
    void resetShortcut(const QString &id);
    void resetAllShortcuts();

    // H-STORE
    void save(QSettings &settings) const;
    void load(QSettings &settings);

    // H-MENU
    void populateMenuBar(QMenuBar *menuBar) const;
    void populateMenu(QMenu *menu, const QString &menuPath) const;

signals:
    void shortcutChanged(const QString &id);

private:
    struct Private;
    Private *d;

    // H-MENU helpers (actionmenus.cpp).
    static QMenu *branchMenu(QMenu *root, const QString &rootPath, const QString &path);
    static void fillBranch(const Private *d, QMenu *root, const QString &rootPath);
};

}
