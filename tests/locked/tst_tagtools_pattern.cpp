// LOCKED ACCEPTANCE TEST (tagging tools: a pattern made ready once; no padding where a file cannot hold it). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/tagging-tools.md (G-INFER,
// G-RENUMBER). What InferPattern must answer is what the functions that
// were there before answer for the same pattern and path. No file is read.
#include <QtTest>

#include "tagtools/tagtools.h"

using namespace linuxbee;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

const QStringList kPatterns {
    QStringLiteral("<Track#> - <Title>"),
    QStringLiteral("<Album Artist>/<Album>/<Track#> - <Title>"),
    QStringLiteral("<Album>/<Disc#>-<Track#> <Title>"),
    QStringLiteral("<Artist> - <Album> (<Year>)/<Title>"),
    QStringLiteral("<Title>"),
    QStringLiteral("x<Title>x"),
    // Not usable:
    QString(),
    QStringLiteral("no field"),
    QStringLiteral("<Nonsense>"),
    QStringLiteral("<Title"),
    QStringLiteral("<Title><Artist>"),
    QStringLiteral("<Title> - <Title>"),
};

const QStringList kPaths {
    QStringLiteral("/music/The Band/Great Album/03 - Some Song.flac"),
    QStringLiteral("/music/The Band/Great Album/1-07 Other.mp3"),
    QStringLiteral("/music/Someone - Record (1999)/A Tune.ogg"),
    QStringLiteral("/music/loose.flac"),
    QStringLiteral("/music/a/b/.hidden"),
    QStringLiteral("/music/a/b/xmiddlex.m4a"),
    QStringLiteral("03 - Bare.flac"),
    QStringLiteral("/music/a/b/12 -   Spaced  .flac"),
    QString(),
};

TrackTags tagsOf(const QString &format, const QString &track = QString())
{
    TrackTags t;
    t.format = format;
    if (!track.isNull())
        t.fields.insert(QStringLiteral("Track#"), { track });
    return t;
}

}

class TagToolsPatternTest : public QObject {
    Q_OBJECT
private slots:
    void theSameAnswers();
    void madeReadyOnce();
    void noPaddingWhereItCannotBeHeld();
};

void TagToolsPatternTest::theSameAnswers()
{
    // G-INFER: InferPattern answers what validateInferPattern, inferFromPath and inferTags answer.
    TrackTags some;
    some.fields.insert(s(u"Title"), { s(u"had a title") });
    int matched = 0;
    for (const QString &pattern : kPatterns) {
        const InferPattern ready(pattern);
        QCOMPARE(ready.problem(), validateInferPattern(pattern));
        for (const QString &path : kPaths) {
            const InferResult want = inferFromPath(path, pattern);
            const InferResult got = ready.match(path);
            QVERIFY2(got.matched == want.matched, qPrintable(pattern + s(u" | ") + path));
            QVERIFY2(got.values == want.values, qPrintable(pattern + s(u" | ") + path));
            matched += got.matched ? 1 : 0;
            for (const bool blank : { true, false }) {
                const TagChanges a = ready.changes(some, path, blank);
                const TagChanges b = inferTags(some, path, pattern, blank);
                QVERIFY2(a.fields == b.fields && a.other == b.other && a.pictures.has_value() == b.pictures.has_value(),
                    qPrintable(pattern + s(u" | ") + path));
            }
        }
    }
    QVERIFY2(matched >= 8, qPrintable(QString::number(matched))); // the table does exercise matches
    // And said outright, so that both being wrong the same way is not a pass:
    // the extension goes, and what is found is trimmed.
    const InferPattern plain(kPatterns.at(0));
    InferResult found = plain.match(kPaths.at(0));
    QVERIFY(found.matched);
    QCOMPARE(found.values.value(s(u"Track#")), s(u"03"));
    QCOMPARE(found.values.value(s(u"Title")), s(u"Some Song"));
    found = plain.match(kPaths.at(7));
    QVERIFY(found.matched);
    QCOMPARE(found.values.value(s(u"Track#")), s(u"12"));
    QCOMPARE(found.values.value(s(u"Title")), s(u"Spaced"));
    QVERIFY(!plain.match(kPaths.at(4)).matched); // a name that is all extension
    // Only empty fields are filled when that is asked.
    QVERIFY(!plain.changes(some, kPaths.at(0), true).fields.contains(s(u"Title")));
    QCOMPARE(plain.changes(some, kPaths.at(0), true).fields.value(s(u"Track#")), QStringList({ s(u"03") }));
    QCOMPARE(plain.changes(some, kPaths.at(0), false).fields.value(s(u"Title")), QStringList({ s(u"Some Song") }));
    // A copy is the same pattern.
    const InferPattern first(kPatterns.at(1));
    const InferPattern copy = first;
    QCOMPARE(copy.match(kPaths.at(0)).values, first.match(kPaths.at(0)).values);
    QCOMPARE(copy.match(kPaths.at(0)).values.value(s(u"Album")), s(u"Great Album"));
}

void TagToolsPatternTest::madeReadyOnce()
{
    // G-INFER, speed: one pattern is made ready once, however many paths it is asked about.
    qint64 at = inferPatternsPrepared();
    const InferPattern ready(kPatterns.at(1));
    QCOMPARE(inferPatternsPrepared() - at, 1);
    at = inferPatternsPrepared();
    int matched = 0;
    for (int i = 0; i < 5000; ++i)
        matched += ready.match(QStringLiteral("/m/Artist %1/Album %2/%3 - Title.flac").arg(i % 40).arg(i % 300).arg(i % 20)).matched ? 1 : 0;
    QCOMPARE(matched, 5000);
    TrackTags none;
    for (int i = 0; i < 100; ++i)
        QVERIFY(!ready.changes(none, QStringLiteral("/m/A/B/%1 - T.flac").arg(i), true).isEmpty());
    const InferPattern copy = ready;
    QVERIFY(copy.match(kPaths.at(0)).matched);
    QCOMPARE(inferPatternsPrepared() - at, 0);

    // The functions that take the pattern as text make it ready at each call; that is what they are for.
    at = inferPatternsPrepared();
    for (int i = 0; i < 10; ++i)
        QVERIFY(inferFromPath(kPaths.at(0), kPatterns.at(1)).matched);
    QCOMPARE(inferPatternsPrepared() - at, 10);
    // A pattern that cannot be used is never made ready, and validating makes nothing ready.
    at = inferPatternsPrepared();
    const InferPattern bad(s(u"<Nonsense>"));
    QVERIFY(!bad.problem().isEmpty());
    QVERIFY(!bad.match(kPaths.at(0)).matched);
    QVERIFY(bad.changes(none, kPaths.at(0), false).isEmpty());
    QVERIFY(validateInferPattern(kPatterns.at(1)).isEmpty());
    QCOMPARE(inferPatternsPrepared() - at, 0);
}

void TagToolsPatternTest::noPaddingWhereItCannotBeHeld()
{
    // G-RENUMBER: an M4A file stores the track number as a number, so it is written without padding.
    RenumberOptions o;
    o.start = 7;
    o.digits = 3;
    QList<TrackTags> tracks { tagsOf(s(u"flac")), tagsOf(s(u"m4a")), tagsOf(s(u"mp3")), tagsOf(s(u"m4a"), s(u"10")), tagsOf(s(u"flac"), s(u"011")) };
    QList<TagChanges> out = renumberTracks(tracks, o);
    QCOMPARE(out.size(), 5);
    QCOMPARE(out.at(0).fields.value(s(u"Track#")), QStringList({ s(u"007") }));
    QCOMPARE(out.at(1).fields.value(s(u"Track#")), QStringList({ s(u"8") }));
    QCOMPARE(out.at(2).fields.value(s(u"Track#")), QStringList({ s(u"009") }));
    QVERIFY(out.at(3).isEmpty()); // 10 already: nothing to offer, this time and every time
    QVERIFY(out.at(4).isEmpty());
    // What was written is what is read back: applied, a second run offers nothing.
    for (int i = 0; i < 5; ++i)
        for (auto it = out.at(i).fields.cbegin(); it != out.at(i).fields.cend(); ++it)
            tracks[i].fields.insert(it.key(), it.value());
    for (const TagChanges &again : renumberTracks(tracks, o))
        QVERIFY(again.isEmpty());

    // The count is a number everywhere and was never padded.
    o.writeTotal = true;
    out = renumberTracks({ tagsOf(s(u"m4a")), tagsOf(s(u"flac")) }, o);
    QCOMPARE(out.at(0).fields.value(s(u"Track Count")), QStringList({ s(u"2") }));
    QCOMPARE(out.at(0).fields.value(s(u"Track#")), QStringList({ s(u"7") }));
    QCOMPARE(out.at(1).fields.value(s(u"Track#")), QStringList({ s(u"008") }));
    // Without digits nothing differs between the formats; an unknown format is padded as asked.
    o.digits = 0;
    out = renumberTracks({ tagsOf(s(u"m4a")), tagsOf(s(u"flac")) }, o);
    QCOMPARE(out.at(0).fields.value(s(u"Track#")), QStringList({ s(u"7") }));
    QCOMPARE(out.at(1).fields.value(s(u"Track#")), QStringList({ s(u"8") }));
    o.digits = 2;
    o.writeTotal = false;
    out = renumberTracks({ tagsOf(QString()), tagsOf(s(u"M4A")), tagsOf(s(u"m4b")) }, o);
    QCOMPARE(out.at(0).fields.value(s(u"Track#")), QStringList({ s(u"07") }));
    QCOMPARE(out.at(1).fields.value(s(u"Track#")), QStringList({ s(u"08") })); // the format's name is exact
    QCOMPARE(out.at(2).fields.value(s(u"Track#")), QStringList({ s(u"09") }));
}

QTEST_GUILESS_MAIN(TagToolsPatternTest)
#include "tst_tagtools_pattern.moc"
