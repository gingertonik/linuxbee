#pragma once
// The shared guts of ActionRegistry: actionregistry.cpp owns the state,
// actionmenus.cpp reads it to build menus (docs/spec/main-window.md, H-MENU).
#include "actions/actionregistry.h"

#include <QAction>
#include <QHash>
#include <QKeySequence>
#include <QList>

namespace linuxbee::ui {

struct ActionRegistry::Private {
    struct Entry {
        ActionInfo info;
        QAction *action = nullptr;
        QKeySequence def; // the catalogue's hotkey, parsed
        QObject *owner = nullptr; // H-IMPLEMENT: the object that gave this command its behaviour
    };
    QList<Entry> entries; // catalogue order
    QHash<QString, int> index; // id -> position in entries

    int indexOf(const QString &id) const { return index.value(id, -1); }
    static bool sameKeys(const QKeySequence &a, const QKeySequence &b)
    {
        // QKeySequence() == QKeySequence() is false in Qt; empty matches empty only by convention.
        if (a.isEmpty() || b.isEmpty())
            return a.isEmpty() && b.isEmpty();
        return a == b;
    }
    QKeySequence keyOf(const QString &id) const
    {
        const int i = indexOf(id);
        return i < 0 ? QKeySequence() : entries.at(i).action->shortcut();
    }
};

} // namespace linuxbee::ui
