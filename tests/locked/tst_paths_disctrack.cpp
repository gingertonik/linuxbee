// LOCKED ACCEPTANCE TEST (organise: a missing Disc-Track# is written `#`). Written by the
// reviewer, not the builder. The builder makes these pass by changing src/, never by editing this
// file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Rules: docs/spec/paths.md P-MISSING and P-PLACEHOLDER; docs/spec/templates.md T-BLANK.
//   W-H  MusicBee wiki, "How to Create a File-Naming Template": a track with no number is
//        named `Adele\21\# Someone like You.mp3`; the same page drops the field with
//        `$IsNull(<Disc-Track#>,,...)`, so the field itself stays blank.
#include <QtTest>

#include "fields/trackfields.h"
#include "paths/pathbuilder.h"
#include "tags/tagreader.h"

using namespace linuxbee;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

class MapTagSource : public TagSource {
public:
    std::optional<QString> value(const QString &fieldName) const override
    {
        for (auto it = m_values.constBegin(); it != m_values.constEnd(); ++it)
            if (it.key().compare(fieldName, Qt::CaseInsensitive) == 0)
                return it.value();
        return std::nullopt;
    }
    QMap<QString, QString> m_values;
};

MapTagSource adele(const QString &discTrack = QString())
{
    MapTagSource t;
    t.m_values = { { s(u"Album Artist"), s(u"Adele") }, { s(u"Album"), s(u"21") },
        { s(u"Disc-Track#"), discTrack }, { s(u"Title"), s(u"Someone like You") } };
    return t;
}

const QString kNaming = QStringLiteral("<Album Artist>\\<Album>\\<Disc-Track#> <Title>");

BuiltPath build(const QString &tmpl, const TagSource &tags, const PathRules &rules = { })
{
    return buildRelativePath(Template::parse(tmpl), tags, s(u"mp3"), rules);
}

}

class PathsDiscTrackTest : public QObject {
    Q_OBJECT
private slots:
    void defaults();
    void placeholder();
    void bothWays();
    void insideAFunction();
    void otherFieldsStillMissing();
    void anyCase();
    void withoutThePlaceholder();
    void theTemplateOption();
    void aRealTrack();
};

void PathsDiscTrackTest::defaults()
{
    const PathRules rules;
    QCOMPARE(rules.blankAs.size(), 1);
    QCOMPARE(rules.blankAs.value(s(u"Disc-Track#")), s(u"#"));
    QVERIFY(TemplateOptions().blankAs.isEmpty());
}

void PathsDiscTrackTest::placeholder()
{
    // W-H: no track number, so `#`; and the track is not refused for it.
    const BuiltPath built = build(kNaming, adele());
    QCOMPARE(built.status, BuiltPath::Status::Ok);
    QCOMPARE(built.relativePath, s(u"Adele/21/# Someone like You.mp3"));
    QCOMPARE(
        build(kNaming, adele(s(u"01-11"))).relativePath, s(u"Adele/21/01-11 Someone like You.mp3"));
    QCOMPARE(build(kNaming, adele(s(u"   "))).relativePath,
        s(u"Adele/21/# Someone like You.mp3")); // blank
}

void PathsDiscTrackTest::bothWays()
{
    // With "skip when tags are missing" off, still `#`, not "Unknown Disc-Track#".
    PathRules allow;
    allow.skipWhenTagsMissing = false;
    QCOMPARE(build(kNaming, adele(), allow).relativePath, s(u"Adele/21/# Someone like You.mp3"));
    MapTagSource noAlbum = adele();
    noAlbum.m_values.insert(s(u"Album"), QString());
    QCOMPARE(build(kNaming, noAlbum, allow).relativePath,
        s(u"Adele/Unknown Album/# Someone like You.mp3"));
}

void PathsDiscTrackTest::insideAFunction()
{
    // W-H: inside a function the field is blank as it is, so $IsNull leaves it out. (A dot, not
    // a space, after it: T-QUOTE drops an unquoted space at the end of an argument.)
    const QString tmpl
        = QStringLiteral("<Album Artist>\\<Album>\\$IsNull(<Disc-Track#>,,<Disc-Track#>.)<Title>");
    QCOMPARE(build(tmpl, adele()).relativePath, s(u"Adele/21/Someone like You.mp3"));
    QCOMPARE(build(tmpl, adele(s(u"11"))).relativePath, s(u"Adele/21/11.Someone like You.mp3"));
    QCOMPARE(build(QStringLiteral("<Album>\\$Upper(<Disc-Track#>)<Title>"), adele()).relativePath,
        s(u"21/Someone like You.mp3"));
}

void PathsDiscTrackTest::otherFieldsStillMissing()
{
    MapTagSource t = adele();
    t.m_values.insert(s(u"Album"), QString());
    const BuiltPath built = build(kNaming, t);
    QCOMPARE(built.status, BuiltPath::Status::MissingTags);
    QCOMPARE(built.missingFields, QStringList({ s(u"Album") })); // not Disc-Track#
}

void PathsDiscTrackTest::anyCase()
{
    QCOMPARE(build(QStringLiteral("<Album>\\<disc-track#> <Title>"), adele()).relativePath,
        s(u"21/# Someone like You.mp3"));
}

void PathsDiscTrackTest::withoutThePlaceholder()
{
    // A caller that empties the map has the rule of before.
    PathRules plain;
    plain.blankAs.clear();
    const BuiltPath refused = build(kNaming, adele(), plain);
    QCOMPARE(refused.status, BuiltPath::Status::MissingTags);
    QCOMPARE(refused.missingFields, QStringList({ s(u"Disc-Track#") }));
    plain.skipWhenTagsMissing = false;
    QCOMPARE(build(kNaming, adele(), plain).relativePath,
        s(u"Adele/21/Unknown Disc-Track# Someone like You.mp3"));
}

void PathsDiscTrackTest::theTemplateOption()
{
    // T-BLANK: blankAs gives the text of a blank top-level field, before unknownForBlank.
    TemplateOptions options;
    options.blankAs.insert(s(u"Disc-Track#"), s(u"#"));
    const MapTagSource t = adele();
    QCOMPARE(Template::parse(s(u"<Disc-Track#> <Title>")).evaluate(t, options),
        s(u"# Someone like You"));
    QCOMPARE(Template::parse(s(u"[$Lower(<Disc-Track#>)]")).evaluate(t, options), s(u"[]"));
    options.unknownForBlank = true;
    MapTagSource noAlbum = adele();
    noAlbum.m_values.insert(s(u"Album"), QString());
    QCOMPARE(Template::parse(s(u"<Album>/<Disc-Track#>")).evaluate(noAlbum, options),
        s(u"Unknown Album/#"));
    QCOMPARE(Template::parse(s(u"<Disc-Track#>")).evaluate(adele(s(u"2-01")), options), s(u"2-01"));
}

void PathsDiscTrackTest::aRealTrack()
{
    // A file whose tags have no track number: Disc-Track# is blank (F-DISCTRACK), named `#`.
    TrackTags tags;
    tags.fields.insert(s(u"Album Artist"), { s(u"Adele") });
    tags.fields.insert(s(u"Album"), { s(u"21") });
    tags.fields.insert(s(u"Title"), { s(u"Someone like You") });
    tags.fields.insert(s(u"Disc#"), { s(u"1") });
    const TrackFieldSource source(tags, s(u"/music/in/x.mp3"));
    QCOMPARE(source.value(s(u"Disc-Track#")), std::optional<QString>(QString()));
    QCOMPARE(build(kNaming, source).relativePath, s(u"Adele/21/# Someone like You.mp3"));
}

QTEST_GUILESS_MAIN(PathsDiscTrackTest)
#include "tst_paths_disctrack.moc"
