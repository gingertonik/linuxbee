// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Every expected value is a rule in docs/spec/tagging-tools.md (G-x) worked
// by hand. W-TM is the MusicBee wiki's "Tools Menu" page and W-TT its
// "Tagging Tools" page, which say what each tool is for and little more.
#include <QtTest>

#include "tagtools/tagtools.h"

using linuxbee::CaseOptions;
using linuxbee::CaseStyle;
using linuxbee::SearchReplaceOptions;
using linuxbee::TagChanges;
using linuxbee::TrackTags;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

using Map = QMap<QString, QStringList>;

TrackTags track()
{
    TrackTags t;
    t.fields = {
        { s(u"Title"), { s(u"the dark side of the moon (live)") } },
        { s(u"Artist"), { s(u"pink floyd"), s(u"The The") } },
        { s(u"Album"), { s(u"Greatest Hits [Disc 1]") } },
        { s(u"Genre"), { s(u"Rock"), s(u"Prog Rock") } },
        { s(u"Comment"), { s(u"ripped by the cat. a+b=c") } },
        { s(u"Track#"), { s(u"3") } },
        { s(u"Rating"), { s(u"4") } },
    };
    t.other = { { s(u"REPLAYGAIN_TRACK_GAIN"), { s(u"-6.50 dB") } }, { s(u"CATALOGNUMBER"), { s(u"the 1") } } };
    linuxbee::Picture p;
    p.type = s(u"Front Cover");
    t.pictures = { p };
    return t;
}

SearchReplaceOptions find(const char16_t *what, const char16_t *with, const QStringList &fields = { })
{
    SearchReplaceOptions o;
    o.find = s(what);
    o.replace = s(with);
    o.fields = fields;
    return o;
}

bool untouched(const TagChanges &c)
{
    return c.other.isEmpty() && !c.pictures.has_value();
}

}

class TagToolsTest : public QObject {
    Q_OBJECT
private slots:
    void replacePlain();
    void replaceOptions();
    void replaceRegex();
    void replaceValidation();
    void changeCase_data();
    void changeCase();
    void capitalise();
    void remove();
    void renumber();
    void inferPattern_data();
    void inferPattern();
    void inferTags();
    void inferValidation();
    void copyAndSwap();
};

void TagToolsTest::replacePlain()
{
    // G-REPLACE: plain text, case ignored, every occurrence, in the named field.
    TagChanges c = linuxbee::searchReplace(track(), find(u"THE ", u"A ", { s(u"Title") }));
    QCOMPARE(c.fields, Map({ { s(u"Title"), { s(u"A dark side of A moon (live)") } } }));
    QVERIFY(untouched(c));
    // No field named: every catalogue field but Rating. Only fields that change are named.
    c = linuxbee::searchReplace(track(), find(u"rock", u"Metal"));
    QCOMPARE(c.fields, Map({ { s(u"Genre"), { s(u"Metal"), s(u"Prog Metal") } } }));
    // Each value of a field is treated on its own; an unchanged value is kept as it is.
    c = linuxbee::searchReplace(track(), find(u"pink", u"Punk"));
    QCOMPARE(c.fields, Map({ { s(u"Artist"), { s(u"Punk floyd"), s(u"The The") } } }));
    // Nothing found: no change at all.
    QVERIFY(linuxbee::searchReplace(track(), find(u"zzz", u"x")).isEmpty());
    // Replacing with the same text is no change either.
    QVERIFY(linuxbee::searchReplace(track(), find(u"Rock", u"Rock", { s(u"Genre") })).isEmpty());
    // Rating is only touched when it is named. Other keys are never touched.
    QVERIFY(linuxbee::searchReplace(track(), find(u"4", u"5")).isEmpty());
    QCOMPARE(linuxbee::searchReplace(track(), find(u"4", u"5", { s(u"rating") })).fields,
        Map({ { s(u"Rating"), { s(u"5") } } }));
    QVERIFY(linuxbee::searchReplace(track(), find(u"6.50", u"1")).isEmpty());
    // A value that becomes empty is dropped; a field left with no value is removed.
    c = linuxbee::searchReplace(track(), find(u"The The", u"", { s(u"Artist") }));
    QCOMPARE(c.fields, Map({ { s(u"Artist"), { s(u"pink floyd") } } }));
    c = linuxbee::searchReplace(track(), find(u"3", u" ", { s(u"Track#") }));
    QCOMPARE(c.fields, Map({ { s(u"Track#"), { } } }));
    // The result is trimmed.
    c = linuxbee::searchReplace(track(), find(u"(live)", u"", { s(u"Title") }));
    QCOMPARE(c.fields, Map({ { s(u"Title"), { s(u"the dark side of the moon") } } }));
    // Plain text is plain: no character is special.
    c = linuxbee::searchReplace(track(), find(u"a+b", u"$1.", { s(u"Comment") }));
    QCOMPARE(c.fields, Map({ { s(u"Comment"), { s(u"ripped by the cat. $1.=c") } } }));
    c = linuxbee::searchReplace(track(), find(u"[Disc 1]", u"(1)"));
    QCOMPARE(c.fields, Map({ { s(u"Album"), { s(u"Greatest Hits (1)") } } }));
}

void TagToolsTest::replaceOptions()
{
    // G-REPLACE match case
    SearchReplaceOptions o = find(u"the", u"A", { s(u"Artist") });
    o.matchCase = true;
    QVERIFY(linuxbee::searchReplace(track(), o).isEmpty()); // "The The" has no lower-case "the"
    o.find = s(u"The");
    QCOMPARE(linuxbee::searchReplace(track(), o).fields,
        Map({ { s(u"Artist"), { s(u"pink floyd"), s(u"A A") } } }));
    // whole word: "the" in "the cat" but not a part of a word
    TrackTags t;
    t.fields = { { s(u"Title"), { s(u"Then the other; the-end, lathe") } } };
    o = find(u"the", u"X", { s(u"Title") });
    o.wholeWord = true;
    QCOMPARE(linuxbee::searchReplace(t, o).fields, Map({ { s(u"Title"), { s(u"Then X other; X-end, lathe") } } }));
    o.wholeWord = false;
    QCOMPARE(linuxbee::searchReplace(t, o).fields, Map({ { s(u"Title"), { s(u"Xn X oXr; X-end, laX") } } }));
    // Letters of any script make a word: "é" is a letter, so "caf" is not a word in "café".
    t.fields = { { s(u"Title"), { s(u"caf\u00e9 caf") } } };
    o = find(u"caf", u"bar", { s(u"Title") });
    o.wholeWord = true;
    QCOMPARE(linuxbee::searchReplace(t, o).fields, Map({ { s(u"Title"), { s(u"caf\u00e9 bar") } } }));
}

void TagToolsTest::replaceRegex()
{
    // G-REPLACE regular expressions: $1 to $9 are the groups, $$ is a dollar sign.
    SearchReplaceOptions o = find(u"^(.*) \\((live)\\)$", u"$1 [$2]", { s(u"Title") });
    o.regex = true;
    QCOMPARE(linuxbee::searchReplace(track(), o).fields,
        Map({ { s(u"Title"), { s(u"the dark side of the moon [live]") } } }));
    o = find(u"(\\w+) (\\w+)", u"$2, $1 $$5 $x", { s(u"Artist") });
    o.regex = true;
    QCOMPARE(linuxbee::searchReplace(track(), o).fields,
        Map({ { s(u"Artist"), { s(u"floyd, pink $5 $x"), s(u"The, The $5 $x") } } }));
    // Case is ignored unless asked for; a group that did not take part is empty.
    o = find(u"ROCK(x)?", u"[$1]", { s(u"Genre") });
    o.regex = true;
    QCOMPARE(linuxbee::searchReplace(track(), o).fields, Map({ { s(u"Genre"), { s(u"[]"), s(u"Prog []") } } }));
    o.matchCase = true;
    QVERIFY(linuxbee::searchReplace(track(), o).isEmpty());
    // In plain mode the replacement is literal.
    o = find(u"Rock", u"$1", { s(u"Genre") });
    QCOMPARE(linuxbee::searchReplace(track(), o).fields, Map({ { s(u"Genre"), { s(u"$1"), s(u"Prog $1") } } }));
    // Whole word wraps the whole expression.
    o = find(u"pro|rock", u"X", { s(u"Genre") });
    o.regex = true;
    o.wholeWord = true;
    QCOMPARE(linuxbee::searchReplace(track(), o).fields, Map({ { s(u"Genre"), { s(u"X"), s(u"Prog X") } } }));
}

void TagToolsTest::replaceValidation()
{
    QVERIFY(linuxbee::validateSearchReplace(find(u"a", u"b")).isEmpty());
    QVERIFY(linuxbee::validateSearchReplace(find(u"a", u"")).isEmpty());
    QVERIFY(!linuxbee::validateSearchReplace(find(u"", u"b")).isEmpty());
    QVERIFY(!linuxbee::validateSearchReplace(find(u"a", u"b", { s(u"Title"), s(u"Nonsense") })).isEmpty());
    QVERIFY(linuxbee::validateSearchReplace(find(u"(", u"b")).isEmpty()); // plain text
    SearchReplaceOptions o = find(u"(", u"b");
    o.regex = true;
    QVERIFY(!linuxbee::validateSearchReplace(o).isEmpty());
    // Options that do not validate change nothing.
    QVERIFY(linuxbee::searchReplace(track(), o).isEmpty());
    QVERIFY(linuxbee::searchReplace(track(), find(u"", u"b")).isEmpty());
    QVERIFY(linuxbee::searchReplace(track(), find(u"e", u"b", { s(u"Nonsense") })).isEmpty());
}

void TagToolsTest::changeCase_data()
{
    QTest::addColumn<int>("style");
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("expected");
    auto row = [](const char *name, CaseStyle style, const char16_t *text, const char16_t *expected) {
        QTest::newRow(name) << int(style) << QString::fromUtf16(text) << QString::fromUtf16(expected);
    };
    // G-CASE EachWord (W-TM: "Capitalises the first letter of all words"): the
    // first letter of each word goes up; nothing else moves.
    row("each", CaseStyle::EachWord, u"the dark side of the moon", u"The Dark Side Of The Moon");
    row("each-keeps-rest", CaseStyle::EachWord, u"mcCartney and DJ shadow", u"McCartney And DJ Shadow");
    row("each-brackets", CaseStyle::EachWord, u"moon (live) [remix] \"quoted\"", u"Moon (Live) [Remix] \"Quoted\"");
    row("each-spacing", CaseStyle::EachWord, u"  two  spaces\tand tab ", u"  Two  Spaces\tAnd Tab ");
    row("each-digits", CaseStyle::EachWord, u"1st 2pac 99 problems", u"1st 2pac 99 Problems"); // a word that starts with a digit is left
    row("each-apostrophe", CaseStyle::EachWord, u"don't stop 'til you", u"Don't Stop 'Til You");
    row("each-hyphen", CaseStyle::EachWord, u"up-tempo non-stop", u"Up-tempo Non-stop"); // a hyphen does not start a word
    row("each-unicode", CaseStyle::EachWord, u"\u00e9cole \u00fcber stra\u00dfe", u"\u00c9cole \u00dcber Stra\u00dfe");
    row("each-empty", CaseStyle::EachWord, u"", u"");
    // TitleCase: small words stay down unless first or last; other words get
    // a capital and lose any others; a word with a capital inside is left.
    row("title", CaseStyle::TitleCase, u"the dark side of the moon", u"The Dark Side of the Moon");
    row("title-from-upper-first", CaseStyle::TitleCase, u"The Dark Side Of The Moon", u"The Dark Side of the Moon");
    row("title-last-small", CaseStyle::TitleCase, u"what are you waiting for", u"What Are You Waiting For");
    row("title-inner-upper", CaseStyle::TitleCase, u"a day with DJ mcCartney and AC/DC", u"A Day with DJ mcCartney and AC/DC");
    row("title-brackets", CaseStyle::TitleCase, u"song of (the) south (live)", u"Song of (the) South (Live)");
    row("title-one-word", CaseStyle::TitleCase, u"the", u"The");
    row("title-spaces", CaseStyle::TitleCase, u" of mice  and men ", u" Of Mice  and Men ");
    // Sentence: the first letter up, everything else down.
    row("sentence", CaseStyle::Sentence, u"THE DARK side Of the Moon", u"The dark side of the moon");
    row("sentence-leading", CaseStyle::Sentence, u" (live) AT Leeds", u" (Live) at leeds");
    row("upper", CaseStyle::Upper, u"Stra\u00dfe caf\u00e9", u"STRASSE CAF\u00c9");
    row("lower", CaseStyle::Lower, u"The DARK \u00c9cole", u"the dark \u00e9cole");
}

void TagToolsTest::changeCase()
{
    QFETCH(int, style);
    QFETCH(QString, text);
    QFETCH(QString, expected);
    CaseOptions o;
    o.style = CaseStyle(style);
    QCOMPARE(linuxbee::changeCase(text, o), expected);
}

void TagToolsTest::capitalise()
{
    // G-CASE: the default small words.
    QCOMPARE(linuxbee::defaultSmallWords().join(QLatin1Char(' ')),
        s(u"a an and as at but by for from in into nor of off on onto or over per the to via with"));
    // No field named: Title, Artist, Album, Album Artist. Album does not change here.
    CaseOptions o;
    TagChanges c = linuxbee::capitalise(track(), o);
    QCOMPARE(c.fields,
        Map({ { s(u"Title"), { s(u"The Dark Side Of The Moon (Live)") } },
            { s(u"Artist"), { s(u"Pink Floyd"), s(u"The The") } } }));
    QVERIFY(untouched(c));
    o.fields = { s(u"comment") };
    o.style = CaseStyle::Upper;
    QCOMPARE(linuxbee::capitalise(track(), o).fields, Map({ { s(u"Comment"), { s(u"RIPPED BY THE CAT. A+B=C") } } }));
    // The caller's own small words replace the default list.
    o = CaseOptions();
    o.style = CaseStyle::TitleCase;
    o.smallWords = { s(u"side"), s(u"MOON") };
    QCOMPARE(linuxbee::changeCase(s(u"the dark side of the moon rises"), o), s(u"The Dark side Of The moon Rises"));
    // An unknown field: nothing at all.
    o.fields = { s(u"Title"), s(u"Nonsense") };
    QVERIFY(linuxbee::capitalise(track(), o).isEmpty());
    // Already as it should be: no change.
    TrackTags done;
    done.fields = { { s(u"Title"), { s(u"Fine Already") } } };
    QVERIFY(linuxbee::capitalise(done, CaseOptions()).isEmpty());
}

void TagToolsTest::remove()
{
    // G-REMOVE (W-TT: "remove all" or "remove specific tags")
    linuxbee::RemoveOptions o;
    QVERIFY(linuxbee::removeTags(track(), o).isEmpty());
    o.fields = { s(u"genre"), s(u"Comment"), s(u"Composer"), s(u"CATALOGNUMBER"), s(u"NOSUCHKEY") };
    TagChanges c = linuxbee::removeTags(track(), o);
    // Composer has no value, so it is not named; an other key is matched exactly.
    QCOMPARE(c.fields, Map({ { s(u"Genre"), { } }, { s(u"Comment"), { } } }));
    QCOMPARE(c.other, Map({ { s(u"CATALOGNUMBER"), { } } }));
    QVERIFY(!c.pictures.has_value());
    o.fields = { s(u"catalognumber") };
    QVERIFY(linuxbee::removeTags(track(), o).isEmpty());

    o = linuxbee::RemoveOptions();
    o.allTags = true;
    c = linuxbee::removeTags(track(), o);
    QCOMPARE(c.fields.keys(), track().fields.keys());
    QCOMPARE(c.other.keys(), track().other.keys());
    for (const QStringList &v : c.fields)
        QVERIFY(v.isEmpty());
    for (const QStringList &v : c.other)
        QVERIFY(v.isEmpty());
    QVERIFY(!c.pictures.has_value()); // pictures are asked for separately

    o = linuxbee::RemoveOptions();
    o.removePictures = true;
    c = linuxbee::removeTags(track(), o);
    QVERIFY(c.fields.isEmpty() && c.other.isEmpty());
    QVERIFY(c.pictures.has_value());
    QVERIFY(c.pictures->isEmpty());
    TrackTags bare = track();
    bare.pictures.clear();
    QVERIFY(linuxbee::removeTags(bare, o).isEmpty()); // nothing to remove
}

void TagToolsTest::renumber()
{
    // G-RENUMBER (W-TM: "Fix track numbering for an album")
    auto withTrack = [](const char16_t *number, const char16_t *count = nullptr) {
        TrackTags t;
        if (number)
            t.fields.insert(s(u"Track#"), { s(number) });
        if (count)
            t.fields.insert(s(u"Track Count"), { s(count) });
        return t;
    };
    const QList<TrackTags> tracks = { withTrack(u"7"), withTrack(u"2"), withTrack(nullptr), withTrack(u"04", u"4") };
    linuxbee::RenumberOptions o;
    QList<TagChanges> c = linuxbee::renumberTracks(tracks, o);
    QCOMPARE(c.size(), 4);
    QCOMPARE(c.at(0).fields, Map({ { s(u"Track#"), { s(u"1") } } }));
    QVERIFY(c.at(1).isEmpty()); // already 2
    QCOMPARE(c.at(2).fields, Map({ { s(u"Track#"), { s(u"3") } } }));
    QCOMPARE(c.at(3).fields, Map({ { s(u"Track#"), { s(u"4") } } })); // "04" is not "4"
    o.start = 9;
    o.digits = 2;
    o.writeTotal = true;
    c = linuxbee::renumberTracks(tracks, o);
    QCOMPARE(c.at(0).fields, Map({ { s(u"Track#"), { s(u"09") } }, { s(u"Track Count"), { s(u"4") } } }));
    QCOMPARE(c.at(1).fields, Map({ { s(u"Track#"), { s(u"10") } }, { s(u"Track Count"), { s(u"4") } } }));
    QCOMPARE(c.at(3).fields, Map({ { s(u"Track#"), { s(u"12") } } })); // the count is 4 already
    o = linuxbee::RenumberOptions();
    o.start = 0;
    QCOMPARE(linuxbee::renumberTracks({ withTrack(u"5") }, o).at(0).fields, Map({ { s(u"Track#"), { s(u"0") } } }));
    QVERIFY(linuxbee::renumberTracks({ }, o).isEmpty());
    o.start = -1;
    QVERIFY(linuxbee::renumberTracks(tracks, o).isEmpty());
}

void TagToolsTest::inferPattern_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<QString>("pattern");
    QTest::addColumn<QString>("expected"); // "Field=value|Field=value" in field-name order; "-" for no match
    auto row = [](const char *name, const char16_t *path, const char16_t *pattern, const char16_t *expected) {
        QTest::newRow(name) << QString::fromUtf16(path) << QString::fromUtf16(pattern) << QString::fromUtf16(expected);
    };
    // G-INFER: the extension is not part of the name.
    row("artist-title", u"/m/Adele - Hello.mp3", u"<Artist> - <Title>", u"Artist=Adele|Title=Hello");
    row("no-folder", u"Adele - Hello.flac", u"<Artist> - <Title>", u"Artist=Adele|Title=Hello");
    // A text field takes as little as it can, so the first separator decides.
    row("first-separator", u"/m/A - B - C.mp3", u"<Artist> - <Title>", u"Artist=A|Title=B - C");
    // Number fields take digits only, which is what makes "01 Title" work.
    row("track-title", u"/m/01 Hello.mp3", u"<Track#> <Title>", u"Title=Hello|Track#=01");
    row("track-dot", u"/m/07. Rolling in the Deep.mp3", u"<Track#>. <Title>", u"Title=Rolling in the Deep|Track#=07");
    row("track-not-number", u"/m/One Hello.mp3", u"<Track#> <Title>", u"-");
    row("disc-track", u"/m/2-05 Song.mp3", u"<Disc#>-<Track#> <Title>", u"Disc#=2|Title=Song|Track#=05");
    row("year-album", u"/m/Adele/2011 - 21/x.mp3", u"<Year> - <Album>/<Title>", u"Album=21|Title=x|Year=2011");
    // Folders: the pattern is matched against the end of the path, folder for folder.
    row("folders", u"/music/Adele/21/01 - Rolling.mp3", u"<Album Artist>/<Album>/<Track#> - <Title>",
        u"Album=21|Album Artist=Adele|Title=Rolling|Track#=01");
    row("folder-partial", u"/music/xAdele/21/a.mp3", u"Adele/<Album>/<Title>", u"-"); // "Adele" must be the whole folder name
    row("folder-literal", u"/music/Adele/21/a.mp3", u"Adele/<Album>/<Title>", u"Album=21|Title=a");
    row("field-stays-in-folder", u"/m/A/B - C.mp3", u"<Artist> - <Title>", u"Artist=B|Title=C");
    row("too-few-folders", u"x.mp3", u"<Artist>/<Title>", u"-");
    // What is found is trimmed; something must be found.
    row("trimmed", u"/m/Adele  -  Hello .mp3", u"<Artist> - <Title>", u"Artist=Adele|Title=Hello");
    row("blank-part", u"/m/ - Hello.mp3", u"<Artist> - <Title>", u"-");
    row("no-separator", u"/m/AdeleHello.mp3", u"<Artist> - <Title>", u"-");
    // Literal text is literal, and its case matters.
    row("literal-special", u"/m/[Adele] (Hello).mp3", u"[<Artist>] (<Title>)", u"Artist=Adele|Title=Hello");
    row("literal-case", u"/m/Adele FEAT Bob.mp3", u"<Artist> feat <Title>", u"-");
    row("field-case", u"/m/Adele - Hello.mp3", u"<ARTIST> - <title>", u"Artist=Adele|Title=Hello");
    // Dots: only the last one in the file name starts the extension.
    row("dots", u"/m/01. Mr. Brightside.mp3", u"<Track#>. <Title>", u"Title=Mr. Brightside|Track#=01");
    row("no-extension", u"/m/Adele - Hello", u"<Artist> - <Title>", u"Artist=Adele|Title=Hello");
    row("hidden", u"/m/.hidden", u"<Title>", u"Title=.hidden");
    row("whole-name", u"/m/Hello.mp3", u"<Title>", u"Title=Hello");
}

void TagToolsTest::inferPattern()
{
    QFETCH(QString, path);
    QFETCH(QString, pattern);
    QFETCH(QString, expected);
    QVERIFY2(linuxbee::validateInferPattern(pattern).isEmpty(), qPrintable(linuxbee::validateInferPattern(pattern)));
    const linuxbee::InferResult r = linuxbee::inferFromPath(path, pattern);
    QStringList got;
    for (auto it = r.values.constBegin(); it != r.values.constEnd(); ++it)
        got.append(it.key() + QLatin1Char('=') + it.value());
    if (expected == QLatin1String("-")) {
        QVERIFY(!r.matched);
        QVERIFY(r.values.isEmpty());
    } else {
        QVERIFY(r.matched);
        QCOMPARE(got.join(QLatin1Char('|')), expected);
    }
}

void TagToolsTest::inferTags()
{
    // G-INFER (W-AT: "fill in tags based on the folder path and filenames")
    const QString path = s(u"/music/Pink Floyd/05 Money.flac");
    const QString pattern = s(u"<Artist>/<Track#> <Title>");
    // Everything found is written, except what the track says already.
    TagChanges c = linuxbee::inferTags(track(), path, pattern, false);
    QCOMPARE(c.fields,
        Map({ { s(u"Artist"), { s(u"Pink Floyd") } }, { s(u"Title"), { s(u"Money") } }, { s(u"Track#"), { s(u"05") } } }));
    QVERIFY(untouched(c));
    // Blank fields only: a field that has a value is left alone.
    QVERIFY(linuxbee::inferTags(track(), path, pattern, true).isEmpty());
    TrackTags partly = track();
    partly.fields.remove(s(u"Title"));
    QCOMPARE(linuxbee::inferTags(partly, path, pattern, true).fields, Map({ { s(u"Title"), { s(u"Money") } } }));
    // The same value is not a change.
    TrackTags same;
    same.fields = { { s(u"Title"), { s(u"Money") } }, { s(u"Track#"), { s(u"05") } } };
    QCOMPARE(linuxbee::inferTags(same, path, pattern, false).fields, Map({ { s(u"Artist"), { s(u"Pink Floyd") } } }));
    // No match: no change.
    QVERIFY(linuxbee::inferTags(track(), s(u"/music/Money.flac"), pattern, false).isEmpty());
    QVERIFY(linuxbee::inferTags(track(), path, s(u"<Nonsense>"), false).isEmpty());
}

void TagToolsTest::inferValidation()
{
    QVERIFY(linuxbee::validateInferPattern(s(u"<Artist> - <Title>")).isEmpty());
    QVERIFY(!linuxbee::validateInferPattern(QString()).isEmpty());
    QVERIFY(!linuxbee::validateInferPattern(s(u"no fields here")).isEmpty());
    QVERIFY(!linuxbee::validateInferPattern(s(u"<Artist> - <Nonsense>")).isEmpty());
    QVERIFY(!linuxbee::validateInferPattern(s(u"<Artist> - <Title")).isEmpty());
    QVERIFY(!linuxbee::validateInferPattern(s(u"<Artist><Title>")).isEmpty()); // nothing says where one ends
    QVERIFY(!linuxbee::validateInferPattern(s(u"<Title> - <title>")).isEmpty()); // twice
    QVERIFY(linuxbee::validateInferPattern(s(u"<Artist>/<Title>")).isEmpty()); // a folder edge is text enough
    QVERIFY(!linuxbee::inferFromPath(s(u"/m/a.mp3"), s(u"<Artist><Title>")).matched);
}

void TagToolsTest::copyAndSwap()
{
    // G-COPY
    TagChanges c = linuxbee::copyTag(track(), s(u"artist"), s(u"Album Artist"));
    QCOMPARE(c.fields, Map({ { s(u"Album Artist"), { s(u"pink floyd"), s(u"The The") } } }));
    QVERIFY(untouched(c));
    // Copying a field that has no value clears the target.
    QCOMPARE(linuxbee::copyTag(track(), s(u"Composer"), s(u"Comment")).fields, Map({ { s(u"Comment"), { } } }));
    QVERIFY(linuxbee::copyTag(track(), s(u"Composer"), s(u"Conductor")).isEmpty()); // both blank
    QVERIFY(linuxbee::copyTag(track(), s(u"Title"), s(u"Title")).isEmpty());
    QVERIFY(linuxbee::copyTag(track(), s(u"Title"), s(u"Nonsense")).isEmpty());
    QVERIFY(linuxbee::copyTag(track(), s(u"Nonsense"), s(u"Title")).isEmpty());

    c = linuxbee::swapTags(track(), s(u"Title"), s(u"Album"));
    QCOMPARE(c.fields,
        Map({ { s(u"Title"), { s(u"Greatest Hits [Disc 1]") } },
            { s(u"Album"), { s(u"the dark side of the moon (live)") } } }));
    c = linuxbee::swapTags(track(), s(u"Genre"), s(u"Composer"));
    QCOMPARE(c.fields, Map({ { s(u"Genre"), { } }, { s(u"Composer"), { s(u"Rock"), s(u"Prog Rock") } } }));
    QVERIFY(linuxbee::swapTags(track(), s(u"Title"), s(u"title")).isEmpty());
    QVERIFY(linuxbee::swapTags(track(), s(u"Composer"), s(u"Conductor")).isEmpty());
    QVERIFY(linuxbee::swapTags(track(), s(u"Title"), s(u"Nonsense")).isEmpty());
    TrackTags same;
    same.fields = { { s(u"Artist"), { s(u"X") } }, { s(u"Album Artist"), { s(u"X") } } };
    QVERIFY(linuxbee::swapTags(same, s(u"Artist"), s(u"Album Artist")).isEmpty());
}

QTEST_APPLESS_MAIN(TagToolsTest)
#include "tst_tagtools.moc"
