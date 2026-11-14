// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
//
// docs/spec/settings-import.md, SI-READ: the reader is ok "whatever else the
// file holds", so no shape of element nesting may stop it. Core only: no
// linuxbee_ui is needed.
#include <QtTest>

#include "settingsimport/settingsimport.h"

using namespace linuxbee;

class SettingsImportReviewTest : public QObject {
    Q_OBJECT

private slots:
    void deepNesting()
    {
        // SI-READ: a setting nested 100000 elements deep is passed over; the
        // reader neither crashes nor loses what follows it.
        const int depth = 100000;
        QByteArray bytes = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n<ApplicationSettings>\r\n";
        bytes += "<Before>1</Before>";
        for (int i = 0; i < depth; ++i)
            bytes += "<a>";
        bytes += "x";
        for (int i = 0; i < depth; ++i)
            bytes += "</a>";
        bytes += "<HotKeys><KeyCommandPair><Key>131137</Key><Command>EditSelectAll</Command>"
                 "<IsGlobal>false</IsGlobal></KeyCommandPair></HotKeys>";
        bytes += "<After>2</After></ApplicationSettings>\r\n";
        const MusicBeeSettings m = readMusicBeeSettings(bytes);
        QVERIFY(m.ok);
        QCOMPARE(m.values.value(QStringLiteral("Before")), QStringLiteral("1"));
        QCOMPARE(m.values.value(QStringLiteral("After")), QStringLiteral("2"));
        QVERIFY(!m.values.contains(QStringLiteral("a")));
        QCOMPARE(m.hotkeys.size(), 1);
        QCOMPARE(m.hotkeys.value(0).key, 131137);
    }

    void deepNestingInsideAPart()
    {
        // SI-READ: elements nested inside a Key are not its text.
        const int depth = 100000;
        QByteArray inner;
        for (int i = 0; i < depth; ++i)
            inner += "<b>";
        inner += "9";
        for (int i = 0; i < depth; ++i)
            inner += "</b>";
        const QByteArray bytes = "<?xml version=\"1.0\"?>\n<ApplicationSettings><HotKeys>"
                                 "<KeyCommandPair><Key>65" + inner + "</Key><Command>EditCopy</Command></KeyCommandPair>"
                                 "</HotKeys></ApplicationSettings>\n";
        const MusicBeeSettings m = readMusicBeeSettings(bytes);
        QVERIFY(m.ok);
        QCOMPARE(m.hotkeys.size(), 1);
        QCOMPARE(m.hotkeys.value(0).key, 65);
    }

    void deepAndUnclosed()
    {
        // SI-READ: cut short while deep is still "never closed", not a crash.
        QByteArray bytes = "<?xml version=\"1.0\"?>\n<ApplicationSettings>";
        for (int i = 0; i < 100000; ++i)
            bytes += "<a>";
        const MusicBeeSettings m = readMusicBeeSettings(bytes);
        QVERIFY(!m.ok);
        QVERIFY(!m.error.isEmpty());
    }
};

QTEST_APPLESS_MAIN(SettingsImportReviewTest)
#include "tst_settingsimport_review.moc"
