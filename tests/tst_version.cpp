#include <QtTest>

#include "version.h"

class VersionTest : public QObject
{
    Q_OBJECT
private slots:
    void matchesProjectVersion()
    {
        // expected: CMakeLists.txt, project(linuxbee VERSION 0.7.1 ...)
        QCOMPARE(linuxbee::versionString(), QStringLiteral("0.7.1"));
    }
};

QTEST_APPLESS_MAIN(VersionTest)
#include "tst_version.moc"
