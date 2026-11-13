// LOCKED ACCEPTANCE TEST (review round 1, plan 10). Written by the reviewer.
// Expected values: docs/spec/main-window.md, H-STORE and W-LAYOUT.
#include <QAction>
#include <QFile>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QtTest>

#include "actions/actionregistry.h"
#include "mainwindow/mainwindow.h"

using linuxbee::ui::ActionRegistry;
using linuxbee::ui::MainWindow;

class ReviewTest : public QObject {
    Q_OBJECT
private slots:
    void swappedHotkeysSurviveLoad();
    void layoutFromFileWrittenByAnotherRun();
};

void ReviewTest::swappedHotkeysSurviveLoad()
{
    // H-STORE: "Every command named in the group first loses its hotkey; then, in
    // catalogue order, each gets the one stored for it." Two commands that swapped
    // their keys both keep the swapped key after a load.
    QTemporaryDir dir;
    QSettings settings(dir.path() + QStringLiteral("/keys.ini"), QSettings::IniFormat);
    const QString play = QStringLiteral("controls.playPause"), stop = QStringLiteral("controls.stop");
    {
        ActionRegistry registry;
        registry.setShortcut(play, QKeySequence());
        QCOMPARE(registry.setShortcut(stop, QKeySequence(QStringLiteral("Ctrl+P"))).status,
            linuxbee::ui::ShortcutResult::Status::Ok);
        QCOMPARE(registry.setShortcut(play, QKeySequence(QStringLiteral("Ctrl+S"))).status,
            linuxbee::ui::ShortcutResult::Status::Ok);
        registry.save(settings);
    }
    ActionRegistry registry;
    registry.load(settings);
    QCOMPARE(registry.shortcut(play), QKeySequence(QStringLiteral("Ctrl+S")));
    QCOMPARE(registry.shortcut(stop), QKeySequence(QStringLiteral("Ctrl+P")));
}

void ReviewTest::layoutFromFileWrittenByAnotherRun()
{
    // W-LAYOUT: the layout a previous run wrote is applied on the next start. An INI
    // file read fresh from disk gives every value back as text, as after a restart.
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/linuxbee.ini");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("[window]\n"
               "navigatorVisible=false\n"
               "rightPanelVisible=true\n"
               "size=@Size(1000 650)\n"
               "splitter=180, 500, 320\n"
               "statusBarVisible=false\n");
    file.close();
    QSettings settings(path, QSettings::IniFormat);
    ActionRegistry actions;
    MainWindow w(&actions, &settings);
    QCOMPARE(w.size(), QSize(1000, 650));
    QVERIFY(!actions.action(QStringLiteral("view.navigator"))->isChecked());
    QVERIFY(actions.action(QStringLiteral("view.rightPanel"))->isChecked());
    QVERIFY(!actions.action(QStringLiteral("view.statusBar"))->isChecked());
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    QVERIFY(!w.findChild<QWidget *>(QStringLiteral("navigatorPanel"))->isVisible());
    QVERIFY(!w.statusBar()->isVisible());
    QTRY_VERIFY(qAbs(w.findChild<QWidget *>(QStringLiteral("rightPanel"))->width() - 320) <= 2);
    // The hidden navigator comes back at the width the file gave it.
    actions.action(QStringLiteral("view.navigator"))->trigger();
    QTRY_VERIFY(w.findChild<QWidget *>(QStringLiteral("navigatorPanel"))->isVisible());
    QTRY_VERIFY(qAbs(w.findChild<QWidget *>(QStringLiteral("navigatorPanel"))->width() - 180) <= 2);
}

QTEST_MAIN(ReviewTest)
#include "tst_ui_mainwindow_restart.moc"
