// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Where each expected value comes from:
//   W-H   MusicBee wiki, "How to Create a File-Naming Template" (the example
//         path Adele\21\01-11 Someone like You.mp3)
//         https://musicbee.fandom.com/wiki/How_to_Create_a_File-Naming_Template
//   W-O   MusicBee wiki, "File Organization"
//         https://musicbee.fandom.com/wiki/File_Organization
//   W-T   MusicBee wiki, "Templates"  https://musicbee.fandom.com/wiki/Templates
//   P-x   a rule in docs/spec/paths.md, worked by hand in the comment beside
//         the row. The public docs are silent on these.
//   MATH  byte counting shown in the comment
#include <QtTest>

#include "paths/pathbuilder.h"

using linuxbee::BuiltPath;
using linuxbee::PathRules;
using linuxbee::Template;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

class MapTagSource : public linuxbee::TagSource {
public:
    std::optional<QString> value(const QString &fieldName) const override
    {
        const auto it = m_values.constFind(fieldName);
        if (it == m_values.constEnd())
            return std::nullopt;
        return *it;
    }
    QHash<QString, QString> m_values;
};

MapTagSource adele()
{
    MapTagSource t;
    t.m_values = {
        { s(u"Album Artist"), s(u"Adele") },
        { s(u"Album"), s(u"21") },
        { s(u"Disc-Track#"), s(u"01-11") },
        { s(u"Title"), s(u"Someone like You") },
        { s(u"Artist"), s(u"AC/DC") },
        { s(u"Blank"), QString() },
    };
    return t;
}

BuiltPath build(const QString &tmpl, const MapTagSource &tags, const QString &ext = s(u"mp3"),
    const PathRules &rules = { })
{
    return linuxbee::buildRelativePath(Template::parse(tmpl), tags, ext, rules);
}

// One field named V with the given value, template "<V>", extension mp3.
QString one(const QString &value, const PathRules &rules = { })
{
    MapTagSource t;
    t.m_values = { { s(u"V"), value } };
    const BuiltPath built = build(s(u"<V>"), t, s(u"mp3"), rules);
    if (built.status != BuiltPath::Status::Ok)
        return s(u"<status not Ok>");
    return built.relativePath;
}

}

class PathsTest : public QObject {
    Q_OBJECT
private slots:
    void wikiExample();
    void separators();
    void characterMapping_data();
    void characterMapping();
    void customMapping();
    void trimming_data();
    void trimming();
    void traversalIsImpossible();
    void missingTags();
    void emptyComponentsAndNames();
    void invalidTemplate();
    void reservedNames();
    void extension_data();
    void extension();
    void length();
    void cleanComponent_data();
    void cleanComponent();
    void uniqueNames();
    void topLevelFields();
};

void PathsTest::wikiExample()
{
    // W-O: the default naming template. W-H: the path it gives.
    const BuiltPath built = build(s(uR"~(<Album Artist>\<Album>\<Disc-Track#> <Title>)~"), adele());
    QCOMPARE(built.status, BuiltPath::Status::Ok);
    QCOMPARE(built.relativePath, s(u"Adele/21/01-11 Someone like You.mp3"));
    QVERIFY(built.missingFields.isEmpty());
}

void PathsTest::separators()
{
    // P-SEP: '/' in the template text separates folders too.
    QCOMPARE(build(s(u"<Album Artist>/<Album>/<Title>"), adele()).relativePath,
        s(u"Adele/21/Someone like You.mp3"));
    // P-SEP: both kinds may be mixed.
    QCOMPARE(build(s(uR"~(<Album Artist>\<Album>/<Title>)~"), adele()).relativePath,
        s(u"Adele/21/Someone like You.mp3"));
    // P-SEP: a '/' inside a tag value is never a separator. Default map: '/' -> ' '.
    QCOMPARE(build(s(uR"~(<Artist>\<Title>)~"), adele()).relativePath,
        s(u"AC DC/Someone like You.mp3"));
    // P-SEP: nor is a '\' inside a tag value.
    MapTagSource t = adele();
    t.m_values.insert(s(u"Artist"), s(uR"~(A\B)~"));
    QCOMPARE(build(s(uR"~(<Artist>\<Title>)~"), t).relativePath, s(u"A B/Someone like You.mp3"));
    // P-SEP: a separator written inside a function argument is a separator.
    QCOMPARE(build(s(uR"~(<Album Artist>\$IsNull(<Album>,,<Album>\)<Title>)~"), adele()).relativePath,
        s(u"Adele/21/Someone like You.mp3"));
    // P-SEP: a leading separator does not make the path absolute.
    QCOMPARE(build(s(uR"~(\<Album Artist>\<Title>)~"), adele()).relativePath,
        s(u"Adele/Someone like You.mp3"));
}

void PathsTest::characterMapping_data()
{
    QTest::addColumn<QString>("value");
    QTest::addColumn<QString>("expected");
    // P-MAP default: each of " | ? < : \ > * / becomes a space (W-O), then
    // P-SPACES joins runs of spaces and P-TRIM removes the ones at the ends.
    QTest::newRow("question") << s(u"Who?") << s(u"Who.mp3");
    QTest::newRow("colon") << s(u"A: B") << s(u"A B.mp3");
    QTest::newRow("quotes") << s(uR"~(The "Best" Of)~") << s(u"The Best Of.mp3");
    QTest::newRow("pipe-star") << s(u"a|b*c") << s(u"a b c.mp3");
    QTest::newRow("angle") << s(u"a<b>c") << s(u"a b c.mp3");
    // W h a t ? _ " Y e s " : _ < n o > _ * |   ->  What Yes no
    QTest::newRow("everything") << s(uR"~(What? "Yes": <no> *|)~") << s(u"What Yes no.mp3");
    // P-CTRL: control characters are removed, not replaced.
    QTest::newRow("newline") << s(u"line1\nline2") << s(u"line1line2.mp3");
    QTest::newRow("tab") << s(u"A  B\tC") << s(u"A BC.mp3");
    QTest::newRow("nul") << QString::fromUtf16(u"a\0b", 3) << s(u"ab.mp3");
    // P-CTRL: half of a surrogate pair is removed; a whole pair is kept.
    QTest::newRow("lone-surrogate") << QString::fromUtf16(u"a\xD83D" u"b", 3) << s(u"ab.mp3");
    QTest::newRow("whole-pair") << s(u"a\U0001F600" u"b") << s(u"a\U0001F600" u"b.mp3");
    // Other scripts and accents pass through untouched.
    QTest::newRow("non-latin") << s(u"\u65e5\u672c\u8a9e \u00e9") << s(u"\u65e5\u672c\u8a9e \u00e9.mp3");
    QTest::newRow("plain") << s(u"plain") << s(u"plain.mp3");
}

void PathsTest::characterMapping()
{
    QFETCH(QString, value);
    QFETCH(QString, expected);
    QCOMPARE(one(value), expected);
}

void PathsTest::customMapping()
{
    PathRules rules;
    // W-O: the user may type a different replacement, or delete it to remove
    // the character outright.
    rules.characterMap.insert(QLatin1Char(' '), s(u"_"));
    rules.characterMap.insert(QLatin1Char('?'), QString());
    rules.characterMap.insert(QLatin1Char(':'), s(u" -"));
    // P-SPACES order: map, join runs of spaces, trim, and only then apply the
    // replacement for the space itself.
    QCOMPARE(one(s(u"Someone like You"), rules), s(u"Someone_like_You.mp3"));
    QCOMPARE(one(s(u"Who?"), rules), s(u"Who.mp3"));
    QCOMPARE(one(s(u"A: B"), rules), s(u"A_-_B.mp3"));
    QCOMPARE(one(s(u" padded "), rules), s(u"padded.mp3"));
    // P-MAP: a map with no entry for '/' still never lets one through.
    PathRules bare;
    bare.characterMap.clear();
    QCOMPARE(one(s(uR"~(a/b\c?d)~"), bare), s(u"abc?d.mp3"));
    // P-MAP: a replacement may not smuggle a separator in.
    PathRules sneaky;
    sneaky.characterMap.insert(QLatin1Char('?'), s(u"/"));
    QCOMPARE(one(s(u"a?b"), sneaky), s(u"ab.mp3"));
    // P-MAP: the space's own replacement (applied in P-SPACES) may not
    // smuggle a separator in either, so no ".." component can appear.
    PathRules spaceSlash;
    spaceSlash.characterMap.insert(QLatin1Char(' '), s(u"/"));
    QCOMPARE(one(s(u"x .. y"), spaceSlash), s(u"x..y.mp3"));
    PathRules spaceBack;
    spaceBack.characterMap.insert(QLatin1Char(' '), s(uR"~(_\_)~"));
    QCOMPARE(linuxbee::cleanComponent(s(u"A B"), spaceBack), s(u"A__B"));
    // T-UNKNOWN then P-MAP, default rules.
    MapTagSource none;
    QCOMPARE(build(s(u"<Nope>"), none).relativePath, s(u"Nope.mp3"));
}

void PathsTest::trimming_data()
{
    QTest::addColumn<QString>("value");
    QTest::addColumn<QString>("expected");
    // P-TRIM: spaces and dots are removed from both ends of every component.
    QTest::newRow("spaces") << s(u"  Spaces  ") << s(u"Spaces.mp3");
    QTest::newRow("trailing-dot") << s(u"Live.") << s(u"Live.mp3");
    QTest::newRow("leading-dots") << s(u"...Baby One More Time") << s(u"Baby One More Time.mp3");
    QTest::newRow("inner-dots-kept") << s(u"T.N.T.") << s(u"T.N.T.mp3");
    QTest::newRow("dot-space-mix") << s(u" . .x. . ") << s(u"x.mp3");
    // P-SPACES
    QTest::newRow("inner-run") << s(u"a    b") << s(u"a b.mp3");
}

void PathsTest::trimming()
{
    QFETCH(QString, value);
    QFETCH(QString, expected);
    QCOMPARE(one(value), expected);
}

void PathsTest::traversalIsImpossible()
{
    MapTagSource t;
    t.m_values = { { s(u"Artist"), s(u"..") }, { s(u"Title"), s(u"../../etc/passwd") } };
    // Artist: ".." trims to nothing and the component is dropped (P-EMPTY).
    // Title: '/' -> ' ' gives ".. .. etc passwd"; P-TRIM gives "etc passwd".
    const BuiltPath built = build(s(uR"~(<Artist>\<Title>)~"), t);
    QCOMPARE(built.status, BuiltPath::Status::Ok);
    QCOMPARE(built.relativePath, s(u"etc passwd.mp3"));

    // The same holds when the dots are written in the template itself.
    const BuiltPath literal = build(s(u"../../<Title>"), t);
    QCOMPARE(literal.relativePath, s(u"etc passwd.mp3"));

    // No result ever starts with '/' or has a "." or ".." component.
    const QStringList hostile = { s(u"/etc/passwd"), s(u"..\\..\\x"), s(u"./x"), s(u"a/../b"),
        s(u"/"), s(u"....//....//x") };
    for (const QString &value : hostile) {
        t.m_values.insert(s(u"Title"), value);
        for (const QString &tmpl : { s(u"<Title>"), s(u"x/<Title>/y"), s(u"/<Title>") }) {
            const BuiltPath b = build(tmpl, t);
            if (b.status != BuiltPath::Status::Ok)
                continue;
            QVERIFY2(!b.relativePath.startsWith(QLatin1Char('/')), qPrintable(b.relativePath));
            const QStringList parts = b.relativePath.split(QLatin1Char('/'));
            for (const QString &part : parts) {
                QVERIFY2(!part.isEmpty(), qPrintable(b.relativePath));
                QVERIFY2(part != s(u".") && part != s(u".."), qPrintable(b.relativePath));
                QVERIFY2(!part.contains(QLatin1Char('\\')), qPrintable(b.relativePath));
            }
        }
    }
}

void PathsTest::missingTags()
{
    MapTagSource t = adele();
    t.m_values.insert(s(u"Album"), QString());
    const QString tmpl = s(uR"~(<Album Artist>\<Album>\<Blank> <Title>)~");

    // W-O: by default a file missing a tag named in the template is not organised.
    const BuiltPath refused = build(tmpl, t);
    QCOMPARE(refused.status, BuiltPath::Status::MissingTags);
    QCOMPARE(refused.missingFields, QStringList({ s(u"Album"), s(u"Blank") }));
    QVERIFY(refused.relativePath.isEmpty());

    // W-O: with the option off, each missing tag becomes "Unknown X".
    PathRules allow;
    allow.skipWhenTagsMissing = false;
    const BuiltPath filled = build(tmpl, t, s(u"mp3"), allow);
    QCOMPARE(filled.status, BuiltPath::Status::Ok);
    QCOMPARE(filled.relativePath, s(u"Adele/Unknown Album/Unknown Blank Someone like You.mp3"));

    // W-T: $IsNull(<Tag>,,<Tag>) keeps a blank tag from counting as missing,
    // because the field is no longer at the top level (P-MISSING).
    const BuiltPath idiom = build(s(uR"~(<Album Artist>\$IsNull(<Album>,,<Album>\)<Title>)~"), t);
    QCOMPARE(idiom.status, BuiltPath::Status::Ok);
    QCOMPARE(idiom.relativePath, s(u"Adele/Someone like You.mp3"));

    // P-MISSING: a value of only spaces is blank. A field the source does not
    // know at all is not "missing" (T-UNKNOWN prints it).
    t.m_values.insert(s(u"Album"), s(u"   "));
    QCOMPARE(build(s(uR"~(<Album>\<Title>)~"), t).status, BuiltPath::Status::MissingTags);
    QCOMPARE(build(s(uR"~(<Nope>\<Title>)~"), t).relativePath, s(u"Nope/Someone like You.mp3"));
}

void PathsTest::emptyComponentsAndNames()
{
    // P-EMPTY: an empty component is dropped.
    QCOMPARE(build(s(uR"~(<Album Artist>\\<Title>)~"), adele()).relativePath,
        s(u"Adele/Someone like You.mp3"));
    QCOMPARE(build(s(uR"~(<Album Artist>\ . \<Title>)~"), adele()).relativePath,
        s(u"Adele/Someone like You.mp3"));
    // P-EMPTY: nothing left for the file name is a refusal, not a guess.
    MapTagSource t;
    t.m_values = { { s(u"Title"), s(u"...") } };
    const BuiltPath dots = build(s(u"<Title>"), t);
    QCOMPARE(dots.status, BuiltPath::Status::EmptyName);
    QVERIFY(dots.relativePath.isEmpty());
    QCOMPARE(build(QString(), t).status, BuiltPath::Status::EmptyName);
    // P-EMPTY: a trailing separator is an empty component like any other, so
    // the last component that is left names the file.
    QCOMPARE(build(s(uR"~(<Title>x\)~"), t).status, BuiltPath::Status::Ok);
    QCOMPARE(build(s(uR"~(<Title>x\)~"), t).relativePath, s(u"x.mp3"));
}

void PathsTest::invalidTemplate()
{
    const BuiltPath built = build(s(u"ab $Nope(x)"), adele());
    QCOMPARE(built.status, BuiltPath::Status::InvalidTemplate);
    QCOMPARE(built.templateError.position, 3); // docs/spec/templates.md, T-ERRORS
    QVERIFY(built.relativePath.isEmpty());
}

void PathsTest::reservedNames()
{
    MapTagSource t;
    t.m_values = { { s(u"Artist"), s(u"CON") }, { s(u"Title"), s(u"aux.live") } };
    // P-RESERVED: the part before the first dot is compared, ignoring case,
    // and gets an underscore.
    QCOMPARE(build(s(uR"~(<Artist>\<Title>)~"), t).relativePath, s(u"CON_/aux_.live.mp3"));
    PathRules off;
    off.avoidReservedNames = false;
    QCOMPARE(build(s(uR"~(<Artist>\<Title>)~"), t, s(u"mp3"), off).relativePath,
        s(u"CON/aux.live.mp3"));
    QCOMPARE(one(s(u"Lpt9")), s(u"Lpt9_.mp3"));
    QCOMPARE(one(s(u"COM0")), s(u"COM0.mp3")); // only COM1 to COM9
    QCOMPARE(one(s(u"Console")), s(u"Console.mp3"));
}

void PathsTest::extension_data()
{
    QTest::addColumn<QString>("ext");
    QTest::addColumn<QString>("expected");
    // P-EXT
    QTest::newRow("plain") << s(u"flac") << s(u"x.flac");
    QTest::newRow("leading-dot") << s(u".flac") << s(u"x.flac");
    QTest::newRow("case-kept") << s(u".MP3") << s(u"x.MP3");
    QTest::newRow("empty") << QString() << s(u"x");
    QTest::newRow("junk-removed") << s(u"m/p 3?") << s(u"x.mp3");
    QTest::newRow("only-junk") << s(u"..") << s(u"x");
}

void PathsTest::extension()
{
    QFETCH(QString, ext);
    QFETCH(QString, expected);
    MapTagSource t;
    t.m_values = { { s(u"V"), s(u"x") } };
    QCOMPARE(build(s(u"<V>"), t, ext).relativePath, expected);
}

void PathsTest::length()
{
    // P-LENGTH, MATH: 255 bytes in all. ".mp3" is 4, so the stem keeps 251.
    const QString longTitle(300, QLatin1Char('a'));
    QCOMPARE(one(longTitle), QString(251, QLatin1Char('a')) + s(u".mp3"));

    // MATH: U+00E9 is 2 bytes in UTF-8. 127 of them are 254 bytes; 128 would be 256.
    MapTagSource t;
    t.m_values = { { s(u"Artist"), QString(300, QChar(0x00E9)) }, { s(u"Title"), s(u"t") } };
    QCOMPARE(build(s(uR"~(<Artist>\<Title>)~"), t).relativePath,
        QString(127, QChar(0x00E9)) + s(u"/t.mp3"));

    // MATH: U+1F600 is 4 bytes and two UTF-16 units. ".flac" is 5 bytes, so
    // 250 are left: 62 whole characters (248 bytes). Never half of one.
    QString emoji;
    for (int i = 0; i < 100; ++i)
        emoji += s(u"\U0001F600");
    t.m_values = { { s(u"V"), emoji } };
    const QString name = build(s(u"<V>"), t, s(u"flac")).relativePath;
    QCOMPARE(name, emoji.left(62 * 2) + s(u".flac"));
    QCOMPARE(name.toUtf8().size(), 253);

    // P-LENGTH then P-TRIM: a cut that leaves a space or dot at the end is trimmed.
    // MATH: 250 'a', then a space, then 'b's. Cutting at 251 leaves "aaa...a ".
    const QString spaced = QString(250, QLatin1Char('a')) + s(u" ") + QString(50, QLatin1Char('b'));
    QCOMPARE(one(spaced), QString(250, QLatin1Char('a')) + s(u".mp3"));

    // A smaller limit is honoured.
    PathRules tight;
    tight.maxComponentBytes = 12;
    QCOMPARE(one(s(u"abcdefghijklmnop"), tight), s(u"abcdefgh.mp3")); // MATH: 12 - 4 = 8
}

void PathsTest::cleanComponent_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("expected");
    QTest::newRow("plain") << s(u"Abbey Road") << s(u"Abbey Road");
    QTest::newRow("slashes") << s(uR"~(a/b\c)~") << s(u"a b c");
    QTest::newRow("trim") << s(u" . x . ") << s(u"x");
    QTest::newRow("all-dots") << s(u"....") << QString();
    QTest::newRow("empty") << QString() << QString();
    QTest::newRow("reserved") << s(u"nul") << s(u"nul_");
    QTest::newRow("long") << QString(400, QLatin1Char('z')) << QString(255, QLatin1Char('z'));
}

void PathsTest::cleanComponent()
{
    QFETCH(QString, text);
    QFETCH(QString, expected);
    QCOMPARE(linuxbee::cleanComponent(text), expected);
}

void PathsTest::uniqueNames()
{
    QSet<QString> taken;
    const auto exists = [&taken](const QString &path) { return taken.contains(path); };

    // W-O: "MusicBee will append _# to the new duplicate files". P-DUP: # starts at 1.
    QCOMPARE(linuxbee::uniqueRelativePath(s(u"a/b.mp3"), exists), s(u"a/b.mp3"));
    taken.insert(s(u"a/b.mp3"));
    QCOMPARE(linuxbee::uniqueRelativePath(s(u"a/b.mp3"), exists), s(u"a/b_1.mp3"));
    taken.insert(s(u"a/b_1.mp3"));
    QCOMPARE(linuxbee::uniqueRelativePath(s(u"a/b.mp3"), exists), s(u"a/b_2.mp3"));

    // P-DUP: the number goes before the last extension only.
    taken.insert(s(u"T.N.T.mp3"));
    QCOMPARE(linuxbee::uniqueRelativePath(s(u"T.N.T.mp3"), exists), s(u"T.N.T_1.mp3"));
    taken.insert(s(u"noext"));
    QCOMPARE(linuxbee::uniqueRelativePath(s(u"noext"), exists), s(u"noext_1"));
    // P-DUP: a dot in a folder name is not an extension.
    taken.insert(s(u"v1.0/track"));
    QCOMPARE(linuxbee::uniqueRelativePath(s(u"v1.0/track"), exists), s(u"v1.0/track_1"));

    // P-DUP with P-LENGTH, MATH: the name is already 255 bytes (251 + ".mp3").
    // "_1" needs 2, so the stem drops to 249.
    const QString full = QString(251, QLatin1Char('a')) + s(u".mp3");
    taken.insert(full);
    QCOMPARE(linuxbee::uniqueRelativePath(full, exists),
        QString(249, QLatin1Char('a')) + s(u"_1.mp3"));

    // P-DUP: it gives up instead of looping for ever.
    int calls = 0;
    const auto always = [&calls](const QString &) {
        ++calls;
        return true;
    };
    QCOMPARE(linuxbee::uniqueRelativePath(s(u"x.mp3"), always), QString());
    QCOMPARE(calls, 10000); // the name itself, then _1 to _9999
}

void PathsTest::topLevelFields()
{
    // docs/spec/paths.md, P-MISSING: fields outside every function call, in
    // order of first appearance, each once.
    const Template t
        = Template::parse(s(u"$If(<Rating>>3,<Artist>,<Title>) <Artist> <Nope> <Album> <Artist>"));
    QVERIFY(t.isValid());
    QCOMPARE(t.topLevelFields(), QStringList({ s(u"Artist"), s(u"Nope"), s(u"Album") }));
    QCOMPARE(Template::parse(s(u"$Upper(<Title>)")).topLevelFields(), QStringList());
    QCOMPARE(Template::parse(s(u"$Upper(")).topLevelFields(), QStringList());
}

QTEST_APPLESS_MAIN(PathsTest)
#include "tst_paths.moc"
