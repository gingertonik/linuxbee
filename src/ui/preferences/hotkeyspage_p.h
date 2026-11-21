#pragma once
#include <QKeySequence>
#include <functional>
#include <QMap>

#include "actions/actionregistry.h"
#include "preferences/hotkeyspage.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QKeySequenceEdit;
class QSettings;
class QTableWidget;

// The private half of HotkeysPage. The class is split over
// hotkeyspage.cpp (widgets, table, filter, buttons) and
// hotkeyspage_edit.cpp (assign, reset, resetAll, load, apply, isModified),
// so the struct lives in a header of its own, as the plan requires.
struct linuxbee::ui::HotkeysPage::Private {
    ActionRegistry *actions = nullptr;
    QSettings *settings = nullptr; // not owned

    QTableWidget *table = nullptr;
    QLineEdit *filterEdit = nullptr;
    QKeySequenceEdit *keyEdit = nullptr;
    QLabel *conflictLabel = nullptr;
    QLabel *conflictIcon = nullptr; // HK-BUTTONS: shown while the typed key is taken
    std::function<bool(const QKeySequence &, const QString &)> confirmMove; // HK-BUTTONS
    QPushButton *assignButton = nullptr;
    QPushButton *clearButton = nullptr;
    QPushButton *resetButton = nullptr;
    QPushButton *resetAllButton = nullptr;

    QMap<QString, QKeySequence> pending; // HK-EDIT: keys kept by the page
    bool reportedModified = false; // the last isModified() answer given out
};
