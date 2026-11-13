// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values: docs/spec/main-window.md, W-PANELS, "at once". Whoever
// put a widget into a panel may have handed it pointers to things of its
// own; when it takes the widget out again, the widget must be gone before
// those things are, not at some later turn of the event loop.
#include <QLabel>
#include <QPointer>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include "actions/actionregistry.h"
#include "mainwindow/mainwindow.h"

using namespace linuxbee::ui;

class PanelsTest : public QObject {
    Q_OBJECT
private slots:
    void replacedAtOnce_data();
    void replacedAtOnce();
    void sameWidgetAgain();
    void goesWithTheWindow();
};

void PanelsTest::replacedAtOnce_data()
{
    QTest::addColumn<int>("which");
    QTest::newRow("navigator") << int(MainWindow::Panel::Navigator);
    QTest::newRow("right") << int(MainWindow::Panel::Right);
    QTest::newRow("player bar") << int(MainWindow::Panel::PlayerBar);
}

void PanelsTest::replacedAtOnce()
{
    QFETCH(int, which);
    const auto panel = MainWindow::Panel(which);
    QTemporaryDir dir;
    QSettings settings(dir.path() + QStringLiteral("/s.ini"), QSettings::IniFormat);
    ActionRegistry actions;
    MainWindow window(&actions, &settings);
    QPointer<QWidget> first(new QLabel(QStringLiteral("first")));
    window.setPanelWidget(panel, first);
    QCOMPARE(window.panelWidget(panel), first.data());
    QPointer<QWidget> second(new QLabel(QStringLiteral("second")));
    window.setPanelWidget(panel, second);
    QVERIFY2(first.isNull(), "the widget that was replaced is still alive when the call has returned");
    QCOMPARE(window.panelWidget(panel), second.data());
    window.setPanelWidget(panel, nullptr);
    QVERIFY2(second.isNull(), "the widget of an emptied panel is still alive when the call has returned");
    QVERIFY(window.panelWidget(panel) == nullptr);
    window.setPanelWidget(panel, nullptr); // emptying an empty panel is nothing
    QVERIFY(window.panelWidget(panel) == nullptr);
}

void PanelsTest::sameWidgetAgain()
{
    // Putting in the widget that is there already changes nothing: it is not deleted.
    QTemporaryDir dir;
    QSettings settings(dir.path() + QStringLiteral("/s.ini"), QSettings::IniFormat);
    ActionRegistry actions;
    MainWindow window(&actions, &settings);
    QPointer<QWidget> widget(new QLabel(QStringLiteral("stays")));
    window.setPanelWidget(MainWindow::Panel::Right, widget);
    window.setPanelWidget(MainWindow::Panel::Right, widget);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
    QVERIFY2(!widget.isNull(), "the widget was deleted by being put where it already was");
    QCOMPARE(window.panelWidget(MainWindow::Panel::Right), widget.data());
}

void PanelsTest::goesWithTheWindow()
{
    QTemporaryDir dir;
    QSettings settings(dir.path() + QStringLiteral("/s.ini"), QSettings::IniFormat);
    ActionRegistry actions;
    QPointer<QWidget> widget(new QLabel(QStringLiteral("owned")));
    {
        MainWindow window(&actions, &settings);
        window.setPanelWidget(MainWindow::Panel::Navigator, widget);
    }
    QVERIFY(widget.isNull());
}

QTEST_MAIN(PanelsTest)
#include "tst_ui_mainwindow_panels.moc"
