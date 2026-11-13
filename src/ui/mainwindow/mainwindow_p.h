#pragma once
// The shared guts of MainWindow: mainwindow.cpp builds the frame,
// mainwindow_commands.cpp holds the window's own commands and its remembered
// layout (docs/spec/main-window.md, W-COMMANDS, W-LAYOUT).
#include "mainwindow/mainwindow.h"

#include <QHash>
#include <QPointer>
#include <QStringList>

class QSettings;
class QSplitter;
class QStackedWidget;
class QWidget;

namespace linuxbee::ui {

class ActionRegistry;

struct MainWindow::Private {
    ActionRegistry *actions = nullptr;
    QSettings *settings = nullptr;
    QSplitter *splitter = nullptr;
    QStackedWidget *stack = nullptr;
    QWidget *navigator = nullptr;
    QWidget *right = nullptr;
    QWidget *playerBar = nullptr;
    QHash<QWidget *, QString> pageIds; // pages carry their id; the stack holds their order
    QStringList order; // page ids, in the order added
    QHash<Panel, QWidget *> contents; // W-PANELS: the widget filling each panel, if any
    QHash<Panel, QWidget *> containers; // the panel frames named in W-FRAME
    int widths[3] = { 220, 680, 300 }; // W-LAYOUT: a hidden panel keeps its width for its return
    bool navigatorShown = true;
    bool rightShown = true;
    bool statusBarShown = true;
};

} // namespace linuxbee::ui
