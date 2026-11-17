// LOCKED ACCEPTANCE TEST. Written by the reviewer.
// Proves that the Widgets half builds, links and runs without a display.
#include <QApplication>
#include <QLabel>
#include <QtTest>

#include "app/appinfo.h"
#include "version.h"

class UiAppInfoTest : public QObject {
    Q_OBJECT
private slots:
    void names();
    void widgetsWorkOffscreen();
};

void UiAppInfoTest::names()
{
    QCOMPARE(linuxbee::ui::applicationDisplayName(), QStringLiteral("linuxBee"));
    QCOMPARE(linuxbee::ui::applicationSettingsName(), QStringLiteral("linuxbee"));
    QCOMPARE(linuxbee::ui::versionLine(), QStringLiteral("linuxBee ") + linuxbee::versionString());
    linuxbee::ui::applyApplicationInfo(*qApp);
    QCOMPARE(QCoreApplication::applicationName(), QStringLiteral("linuxbee"));
    QCOMPARE(QCoreApplication::organizationName(), QString()); // main-window.md, W-PLACES: the name once
    QCOMPARE(QCoreApplication::applicationVersion(), linuxbee::versionString());
    QCOMPARE(QGuiApplication::applicationDisplayName(), QStringLiteral("linuxBee"));
    QCOMPARE(QGuiApplication::desktopFileName(), QStringLiteral("linuxbee"));
}

void UiAppInfoTest::widgetsWorkOffscreen()
{
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
    QLabel label(QStringLiteral("hello"));
    label.resize(200, 50);
    label.show();
    QVERIFY(QTest::qWaitForWindowExposed(&label));
    const QImage shot = label.grab().toImage();
    QCOMPARE(shot.size(), QSize(200, 50) * label.devicePixelRatio());
}

QTEST_MAIN(UiAppInfoTest)
#include "tst_ui_appinfo.moc"
