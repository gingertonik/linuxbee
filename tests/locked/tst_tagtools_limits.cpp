// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values: docs/spec/tagging-tools.md, G-RENUMBER, the limit on
// `digits` added after the review of plan 09.
#include <QtTest>

#include "tagtools/tagtools.h"

using namespace linuxbee;

class TagToolsLimitsTest : public QObject {
    Q_OBJECT
private slots:
    void digits();
};

void TagToolsLimitsTest::digits()
{
    TrackTags one;
    one.fields.insert(QStringLiteral("Track#"), { QStringLiteral("5") });
    const QList<TrackTags> tracks { one, one };
    RenumberOptions options;
    options.digits = 9;
    const QList<TagChanges> nine = renumberTracks(tracks, options);
    QCOMPARE(nine.size(), 2);
    QCOMPARE(nine.at(0).fields.value(QStringLiteral("Track#")), QStringList({ QStringLiteral("000000001") }));
    QCOMPARE(nine.at(1).fields.value(QStringLiteral("Track#")), QStringList({ QStringLiteral("000000002") }));
    // More than nine: an empty list, at once, however large the number.
    for (int digits : { 10, 11, 1000, 2000000000, std::numeric_limits<int>::max() }) {
        options.digits = digits;
        QElapsedTimer timer;
        timer.start();
        QCOMPARE(renumberTracks(tracks, options).size(), 0);
        QVERIFY(timer.elapsed() < 2000);
    }
}

QTEST_APPLESS_MAIN(TagToolsLimitsTest)
#include "tst_tagtools_limits.moc"
