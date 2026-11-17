// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/tag-editor.md (E-FIELDS, E-STATE,
// E-SPLIT, E-CHECK, E-BUILD). W-TE is the MusicBee wiki's "Tag Editor" page.
#include <QtTest>

#include "tagedit/tagedit.h"

using namespace linuxbee;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

TrackTags tags(std::initializer_list<std::pair<const char16_t *, QStringList>> fields)
{
    TrackTags t;
    for (const auto &f : fields)
        t.fields.insert(s(f.first), f.second);
    return t;
}

QList<TrackTags> album()
{
    return {
        tags({ { u"Title", { s(u"Hunter") } }, { u"Artist", { s(u"Bj\u00f6rk") } },
            { u"Album", { s(u"Homogenic") } }, { u"Track#", { s(u"1") } },
            { u"Genre", { s(u"Electronic"), s(u"Trip Hop") } }, { u"Year", { s(u"1997") } } }),
        tags({ { u"Title", { s(u"J\u00f3ga") } }, { u"Artist", { s(u"Bj\u00f6rk") } },
            { u"Album", { s(u"Homogenic") } }, { u"Track#", { s(u"2") } },
            { u"Genre", { s(u"Electronic"), s(u"Trip Hop") } }, { u"Rating", { s(u"4.5") } } }),
        tags({ { u"Title", { s(u"Unravel") } },
            { u"Artist", { s(u"Bj\u00f6rk"), s(u"Guy Sigsworth") } },
            { u"Album", { s(u"Homogenic") } }, { u"Track#", { s(u"3") } },
            { u"Genre", { s(u"Trip Hop"), s(u"Electronic") } } }),
    };
}

}

class TagEditTest : public QObject {
    Q_OBJECT
private slots:
    void fields();
    void state();
    void split_data();
    void split();
    void problems_data();
    void problems();
    void buildOne();
    void buildMany();
    void buildSkips();
};

void TagEditTest::fields()
{
    // E-FIELDS
    QCOMPARE(tagEditorFields(),
        QStringList({ s(u"Title"), s(u"Artist"), s(u"Album Artist"), s(u"Album"), s(u"Year"),
            s(u"Track#"), s(u"Track Count"), s(u"Disc#"), s(u"Disc Count"), s(u"Genre"),
            s(u"Composer"), s(u"Grouping"), s(u"Publisher"), s(u"BPM"), s(u"Rating"), s(u"Love"),
            s(u"Comment") }));
    for (const QString &f : tagEditorFields())
        QVERIFY2(tagFieldNames().contains(f), qPrintable(f)); // every one is a catalogue field
    // E-SPLIT: which fields hold lists
    for (const char16_t *f : { u"Artist", u"Genre", u"Composer", u"Conductor", u"Lyricist", u"Mood",
             u"Mix Artist", u"Original Artist" })
        QVERIFY2(isListField(s(f)), qPrintable(s(f)));
    for (const char16_t *f : { u"Title", u"Album", u"Album Artist", u"Year", u"Track#", u"Comment",
             u"Rating", u"Publisher", u"Nonsense", u"" })
        QVERIFY2(!isListField(s(f)), qPrintable(s(f)));
    QVERIFY(isListField(s(u"genre")));
}

void TagEditTest::state()
{
    // E-STATE
    const QList<TrackTags> a = album();
    FieldState st = fieldState(a, s(u"Album"));
    QVERIFY(st.kind == FieldState::Kind::Same);
    QCOMPARE(st.text, s(u"Homogenic"));
    st = fieldState(a, s(u"Title"));
    QVERIFY(st.kind == FieldState::Kind::Mixed);
    QCOMPARE(st.text, QString());
    st = fieldState(a, s(u"Composer"));
    QVERIFY(st.kind == FieldState::Kind::Blank);
    QCOMPARE(st.text, QString());
    QVERIFY(
        fieldState(a, s(u"Year")).kind == FieldState::Kind::Mixed); // one has it, the others do not
    QVERIFY(
        fieldState(a, s(u"Artist")).kind == FieldState::Kind::Mixed); // one more value on the third
    QVERIFY(fieldState(a, s(u"Genre")).kind
        == FieldState::Kind::Mixed); // the same values in another order differ
    st = fieldState(a.mid(0, 2), s(u"Genre"));
    QVERIFY(st.kind == FieldState::Kind::Same);
    QCOMPARE(st.text, s(u"Electronic; Trip Hop"));
    st = fieldState(a.mid(2, 1), s(u"Artist")); // one track: never mixed
    QVERIFY(st.kind == FieldState::Kind::Same);
    QCOMPARE(st.text, s(u"Bj\u00f6rk; Guy Sigsworth"));
    QVERIFY(fieldState(a.mid(0, 1), s(u"Rating")).kind == FieldState::Kind::Blank);
    QCOMPARE(fieldState(a, s(u"album")).text, s(u"Homogenic")); // names are not case sensitive
    QVERIFY(fieldState({ }, s(u"Album")).kind == FieldState::Kind::Blank);
    QVERIFY(fieldState(a, s(u"No Such Field")).kind == FieldState::Kind::Blank);
}

void TagEditTest::split_data()
{
    QTest::addColumn<QString>("field");
    QTest::addColumn<QString>("text");
    QTest::addColumn<QStringList>("expected");
    auto row = [](const char *name, const char16_t *f, const char16_t *t, const QStringList &e) {
        QTest::newRow(name) << s(f) << s(t) << e;
    };
    // E-SPLIT
    row("list", u"Artist", u"Bj\u00f6rk; Guy Sigsworth", { s(u"Bj\u00f6rk"), s(u"Guy Sigsworth") });
    row("list no space", u"Genre", u"Rock;Pop", { s(u"Rock"), s(u"Pop") });
    row("list spaces", u"Genre", u"  Rock ;  Pop  ; ", { s(u"Rock"), s(u"Pop") });
    row("list one", u"Composer", u"Bach", { s(u"Bach") });
    row("list empty parts", u"Genre", u";;Rock;;", { s(u"Rock") });
    row("list repeats", u"Genre", u"Rock; Pop; Rock", { s(u"Rock"), s(u"Pop") });
    row("list repeats differ by case", u"Genre", u"Rock; rock", { s(u"Rock"), s(u"rock") });
    row("list blank", u"Artist", u"   ", { });
    row("list only separators", u"Artist", u" ; ; ", { });
    row("comma is not a separator", u"Artist", u"Earth, Wind & Fire", { s(u"Earth, Wind & Fire") });
    row("single keeps semicolon", u"Title", u"One; Two", { s(u"One; Two") });
    row("single trimmed", u"Album", u"  Homogenic  ", { s(u"Homogenic") });
    row("single inner spaces kept", u"Title", u"A  B", { s(u"A  B") });
    row("single blank", u"Title", u"", { });
    row("single spaces", u"Comment", u"   ", { });
    row("album artist is single", u"Album Artist", u"Simon; Garfunkel", { s(u"Simon; Garfunkel") });
    row("field name case", u"genre", u"Rock; Pop", { s(u"Rock"), s(u"Pop") });
}

void TagEditTest::split()
{
    QFETCH(QString, field);
    QFETCH(QString, text);
    QFETCH(QStringList, expected);
    QCOMPARE(valuesFromText(field, text), expected);
}

void TagEditTest::problems_data()
{
    QTest::addColumn<QString>("field");
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("expected");
    auto row = [](const char *name, const char16_t *f, const char16_t *t, const char16_t *e) {
        QTest::newRow(name) << s(f) << s(t) << s(e);
    };
    // E-CHECK
    row("text", u"Title", u"Anything; at all / really", u"");
    row("blank is fine", u"Track#", u"", u"");
    row("spaces are blank", u"Track#", u"   ", u"");
    row("number", u"Track#", u"7", u"");
    row("number spaces", u"Track#", u" 12 ", u"");
    row("leading zero", u"Disc#", u"02", u"");
    row("four digits", u"Track Count", u"9999", u"");
    row("five digits", u"Track Count", u"10000",
        u"Track Count must be a whole number from 1 to 9999.");
    row("zero", u"Track#", u"0", u"Track# must be a whole number from 1 to 9999.");
    row("slash", u"Track#", u"3/12", u"Track# must be a whole number from 1 to 9999.");
    row("letters", u"Disc#", u"A", u"Disc# must be a whole number from 1 to 9999.");
    row("negative", u"Disc Count", u"-1", u"Disc Count must be a whole number from 1 to 9999.");
    row("decimal", u"Track#", u"1.5", u"Track# must be a whole number from 1 to 9999.");
    row("rating", u"Rating", u"4.5", u"");
    row("rating whole", u"Rating", u"5", u"");
    row("rating half", u"Rating", u"0.5", u"");
    row("rating five point oh", u"Rating", u"5.0", u"");
    row("rating blank", u"Rating", u"", u"");
    row("rating zero", u"Rating", u"0", u"Rating must be from 0.5 to 5 in steps of 0.5.");
    row("rating quarter", u"Rating", u"4.25", u"Rating must be from 0.5 to 5 in steps of 0.5.");
    row("rating high", u"Rating", u"5.5", u"Rating must be from 0.5 to 5 in steps of 0.5.");
    row("rating words", u"Rating", u"five", u"Rating must be from 0.5 to 5 in steps of 0.5.");
    row("year is free", u"Year", u"1997-09-22", u"");
    row("bpm is free", u"BPM", u"120.5", u"");
    row("unknown field", u"Nonsense", u"x", u"Nonsense is not a tag field.");
    row("name case", u"track#", u"x", u"Track# must be a whole number from 1 to 9999.");
}

void TagEditTest::problems()
{
    QFETCH(QString, field);
    QFETCH(QString, text);
    QFETCH(QString, expected);
    QCOMPARE(editProblem(field, text), expected);
}

void TagEditTest::buildOne()
{
    // E-BUILD, one track
    const QList<TrackTags> one = album().mid(0, 1);
    QList<TagChanges> c = buildEdits(one, { });
    QCOMPARE(c.size(), 1);
    QVERIFY(c.at(0).isEmpty());
    c = buildEdits(one,
        { { s(u"Title"), s(u"Hunter (Live)") },
            { s(u"Album"), s(u" Homogenic ") }, // the same once trimmed
            { s(u"Year"), s(u"") }, { s(u"Composer"), s(u"Bj\u00f6rk; Sjon") },
            { s(u"Comment"), s(u"  ") }, { s(u"Rating"), s(u"5.0") },
            { s(u"Genre"), s(u"Trip Hop; Electronic") } });
    QCOMPARE(c.size(), 1);
    const QMap<QString, QStringList> expected = { { s(u"Title"), { s(u"Hunter (Live)") } },
        { s(u"Year"), { } }, { s(u"Composer"), { s(u"Bj\u00f6rk"), s(u"Sjon") } },
        { s(u"Rating"), { s(u"5") } }, { s(u"Genre"), { s(u"Trip Hop"), s(u"Electronic") } } };
    QCOMPARE(c.at(0).fields, expected); // Album and Comment are not named: nothing would change
    QVERIFY(c.at(0).other.isEmpty());
    QVERIFY(!c.at(0).pictures.has_value());
}

void TagEditTest::buildMany()
{
    // E-BUILD, several tracks: each gets only what changes for it.
    const QList<TrackTags> a = album();
    QList<TagChanges> c = buildEdits(a,
        { { s(u"Album"), s(u"Homogenic") }, { s(u"Year"), s(u"1997") },
            { s(u"Artist"), s(u"Bj\u00f6rk") }, { s(u"Rating"), s(u"") } });
    QCOMPARE(c.size(), 3);
    QVERIFY(c.at(0).isEmpty()); // the first already says all of it
    QCOMPARE(c.at(1).fields,
        (QMap<QString, QStringList> { { s(u"Year"), { s(u"1997") } }, { s(u"Rating"), { } } }));
    QCOMPARE(c.at(2).fields,
        (QMap<QString, QStringList> {
            { s(u"Year"), { s(u"1997") } }, { s(u"Artist"), { s(u"Bj\u00f6rk") } } }));
    // A field that is not edited is not touched, however mixed it is.
    for (const TagChanges &one : c)
        QVERIFY(!one.fields.contains(s(u"Title")) && !one.fields.contains(s(u"Track#")));
    QVERIFY(buildEdits({ }, { { s(u"Album"), s(u"x") } }).isEmpty());
}

void TagEditTest::buildSkips()
{
    // E-BUILD: an edit that E-CHECK refuses changes nothing; the rest still apply.
    const QList<TrackTags> one = album().mid(0, 1);
    const QList<TagChanges> c = buildEdits(one,
        { { s(u"Track#"), s(u"1/12") }, { s(u"Rating"), s(u"11") }, { s(u"Nonsense"), s(u"x") },
            { s(u"album"), s(u"Post") }, { s(u"Track Count"), s(u"010") } });
    QCOMPARE(c.size(), 1);
    QCOMPARE(c.at(0).fields,
        (QMap<QString, QStringList> {
            { s(u"Album"), { s(u"Post") } }, { s(u"Track Count"), { s(u"10") } } }));
}

QTEST_APPLESS_MAIN(TagEditTest)
#include "tst_tagedit.moc"
