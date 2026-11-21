#pragma once
#include <QKeySequence>
#include <functional>

#include "actions/actionregistry.h"
#include "preferences/preferencesdialog.h"

class QSettings;

// The Hotkeys page: every command and its key, with search, assign, clear
// and reset. Contract: docs/spec/preferences.md (HK-x).
namespace linuxbee::ui {

class HotkeysPage : public PreferencesPage {
    Q_OBJECT
public:
    // Neither is owned. With settings given, apply() also saves the hotkeys there.
    explicit HotkeysPage(
        ActionRegistry *actions, QSettings *settings = nullptr, QWidget *parent = nullptr);
    ~HotkeysPage() override;

    QString title() const override;
    void load() override;
    QString apply() override;
    bool isModified() const override;

    // HK-EDIT: the edits made on the page and not applied yet.
    QKeySequence pending(const QString &id) const;
    ShortcutResult assign(const QString &id, const QKeySequence &sequence);
    void reset(const QString &id);
    void resetAll();

    // HK-MOVE: as assign(), but a key another command has is taken from it. The answer names
    // that command in conflictsWith.
    ShortcutResult move(const QString &id, const QKeySequence &sequence);
    // HK-BUTTONS: asked by the Assign button before a move; none set means a dialog.
    void setConfirmMove(
        std::function<bool(const QKeySequence &key, const QString &holder)> confirm);

private:
    struct Private;
    Private *d;

    // Private helpers (added under the WORKERS.md rule that private members
    // are open; defined in hotkeyspage.cpp and hotkeyspage_edit.cpp).
    static QString commandLabel(const ActionInfo &info); // HK-TABLE cell text
    int rowForId(const QString &id) const;
    QString idForRow(int row) const;
    void setRowKey(int row, const QKeySequence &key);
    void refreshHotkeyCells();
    void applyFilter(); // HK-FILTER
    void checkModified(); // emit modifiedChanged only when the answer flips
    QString selectedId() const;
};

}
