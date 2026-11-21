// LOCKED ACCEPTANCE TEST (templates: $Date reads dates with slashes; $Group is the first n
// letters). Written by the reviewer, not the builder. The builder makes these pass by changing
// src/, never by editing this file. A test believed to be wrong is reported, with the reason,
// and left alone.
//
// Rules: docs/spec/templates.md, T-DATE and T-GROUP. Sources beside each row:
//   W-F   MusicBee wiki, "Functions": $Group "groups tag values by the first # letters"
//   W-VTF MusicBee wiki, "Useful Virtual Tag Formulas": $Date of a year/month/day text
//   W-H   MusicBee wiki, "How to Create a File-Naming Template": $Group(<Artist>,1) gives T
//   T-x   a linuxBee decision in docs/spec/templates.md (the public pages are silent)
#include <QtTest>

#include "template/template.h"

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

MapTagSource track()
{
    MapTagSource t;
    t.m_values = {
        { s(u"Artist"), s(u"The Beatles") },
        { s(u"Album Artist"), s(u"Deafheaven") },
        { s(u"Album"), QString() },
        { s(u"Title"), s(u"  \u00e9t\u00e9 indien") },
        { s(u"Short"), s(u"ab") },
        { s(u"Band"), s(u"AC/DC") },
        { s(u"Kanji"), s(u"\u65e5\u672c\u8a9e") },
        { s(u"Year"), s(u"2021-12-31") },
        { s(u"Slashed"), s(u"2021/12/31") },
        { s(u"Month"), s(u"1998/03") },
        { s(u"Loose"), s(u"2021/1/5") },
        { s(u"Feb30"), s(u"2021/02/30") },
        { s(u"Month13"), s(u"2021/13") },
        { s(u"Dmy"), s(u"31/12/2021") },
        { s(u"Padded"), s(u" 2021/12/31 ") },
        { s(u"Timed"), s(u"2021/12/31 19:30") },
        { s(u"Leap"), s(u"2020/02/29") },
    };
    return t;
}

void check(const QString &text, const QString &expected)
{
    const Template t = Template::parse(text);
    QVERIFY2(t.isValid(), qPrintable(text));
    QCOMPARE(t.evaluate(track()), expected);
}

void row(const char *name, const char16_t *text, const char16_t *expected)
{
    QTest::newRow(name) << s(text) << s(expected);
}

}

class TemplateDatesGroupsTest : public QObject {
    Q_OBJECT
private slots:
    void dates_data();
    void dates();
    void groups_data();
    void groups();
};

void TemplateDatesGroupsTest::dates_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("expected");
    // As the formula of W-VTF does: a year/month/day text made of the ISO date, then formatted
    // (W-VTF cuts the month out with $Mid, which this version has not; $Left of $Right does it).
    row("wiki formula",
        u"$Date($Left(<Year>,4)/$Left($Right(<Year>,5),2)/$Right(<Year>,2),dd/MM/yyyy)",
        u"31/12/2021");
    row("slashes", u"$Date(<Slashed>,yyyy-MM-dd)", u"2021-12-31"); // W-VTF
    row("slashes, names", u"$Date(<Slashed>,dddd d MMMM yyyy)", u"Friday 31 December 2021");
    row("year and month", u"$Date(<Month>,MMMM yyyy)", u"March 1998"); // T-DATE
    row("year and month is the 1st", u"$Date(<Month>,dd)", u"01"); // T-DATE
    row("one digit", u"$Date(<Loose>,dd/MM/yyyy)", u"05/01/2021"); // T-DATE
    row("spaces round", u"$Date(<Padded>,yyyy)", u"2021"); // T-DATE
    row("leap day", u"$Date(<Leap>,d MMM)", u"29 Feb");
    row("no such day", u"$Date(<Feb30>,yyyy)", u""); // T-DATE
    row("no such month", u"$Date(<Month13>,yyyy)", u""); // T-DATE
    row("day first is not read", u"$Date(<Dmy>,yyyy)", u""); // T-DATE
    row("a time after the slashes", u"$Date(<Timed>,yyyy HH:mm)", u"2021 19:30"); // T-DATE
    row("iso as before", u"$Date(<Year>,yyyy/MM/dd)", u"2021/12/31");
    row("year as before", u"$Date(1999,yy)", u"99");
}

void TemplateDatesGroupsTest::dates()
{
    QFETCH(QString, text);
    QFETCH(QString, expected);
    check(text, expected);
}

void TemplateDatesGroupsTest::groups_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("expected");
    row("one letter", u"$Group(<Artist>,1)", u"T"); // W-H
    row("three", u"$Group(<Album Artist>,3)", u"DEA"); // W-F
    row("four", u"$Group(<Album Artist>,4)", u"DEAF"); // W-F
    row("upper case", u"$Group(<Artist>,5)", u"THE B"); // T-GROUP
    row("trimmed, accent kept", u"$Group(<Title>,3)", u"\u00c9T\u00c9"); // T-GROUP
    row("shorter than n", u"$Group(<Short>,5)", u"AB"); // T-GROUP
    row("not only letters after the first", u"$Group(<Band>,3)", u"AC/"); // T-GROUP
    row("a letter of another script", u"$Group(<Kanji>,2)", u"\u65e5\u672c"); // T-GROUP
    row("a digit first", u"$Group(1999,3)", u"#"); // T-GROUP
    row("punctuation first", u"$Group(\"(live)\",2)", u"#"); // T-GROUP
    row("n of 0", u"$Group(<Artist>,0)", u""); // T-GROUP
    row("n not a count", u"$Group(<Artist>,x)", u""); // T-GROUP
    row("empty", u"$Group(<Album>,2)", u""); // T-GROUP
}

void TemplateDatesGroupsTest::groups()
{
    QFETCH(QString, text);
    QFETCH(QString, expected);
    check(text, expected);
}

QTEST_GUILESS_MAIN(TemplateDatesGroupsTest)
#include "tst_template_dates_groups.moc"
