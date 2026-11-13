// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
//
// Review of plan 09: three places the branch contradicts
// docs/spec/tagging-tools.md. Every expected value is a rule there.
#include <QtTest>

#include <climits>

#include "tagtools/tagtools.h"

using linuxbee::CaseOptions;
using linuxbee::CaseStyle;
using linuxbee::RenumberOptions;
using linuxbee::SearchReplaceOptions;
using linuxbee::TrackTags;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

}

class TagToolsReviewTest : public QObject {
    Q_OBJECT
private slots:
    void invalidExpressionWithWholeWord();
    void caseOutsideBmp();
    void renumberPastIntMax();
};

void TagToolsReviewTest::invalidExpressionWithWholeWord()
{
    // G-REPLACE: validateSearchReplace returns a message when the regular
    // expression is not valid. Whole word must not make an invalid one valid.
    SearchReplaceOptions o;
    o.regex = true;
    o.find = s(u"a)(?:b");
    QVERIFY(!linuxbee::validateSearchReplace(o).isEmpty());
    o.wholeWord = true;
    QVERIFY(!linuxbee::validateSearchReplace(o).isEmpty());
    o.find = s(u"a)|(?:b");
    QVERIFY(!linuxbee::validateSearchReplace(o).isEmpty());
    // ...and searchReplace with options that do not validate returns no changes.
    TrackTags t;
    t.fields.insert(s(u"Title"), { s(u"bab") });
    o.fields = { s(u"Title") };
    o.replace = s(u"X");
    QVERIFY(linuxbee::searchReplace(t, o).isEmpty());
}

void TagToolsReviewTest::caseOutsideBmp()
{
    // G-CASE: "the first letter becomes a capital". U+10428 DESERET SMALL
    // LETTER LONG I is a letter; its capital is U+10400.
    CaseOptions o;
    o.style = CaseStyle::EachWord;
    QCOMPARE(linuxbee::changeCase(s(u"\U00010428abc def"), o), s(u"\U00010400abc Def"));
    o.style = CaseStyle::Sentence;
    QCOMPARE(linuxbee::changeCase(s(u"\U00010428ABC DEF"), o), s(u"\U00010400abc def"));
    // TitleCase: a word with a capital after its first letter is left alone.
    o.style = CaseStyle::TitleCase;
    QCOMPARE(linuxbee::changeCase(s(u"x\U00010400 y"), o), s(u"x\U00010400 Y"));
}

void TagToolsReviewTest::renumberPastIntMax()
{
    // G-RENUMBER: the first track gets `start`, the next `start + 1`.
    RenumberOptions o;
    o.start = INT_MAX;
    const QList<linuxbee::TagChanges> r = linuxbee::renumberTracks({ TrackTags(), TrackTags() }, o);
    QCOMPARE(r.size(), 2);
    QCOMPARE(r.at(0).fields.value(s(u"Track#")), QStringList { s(u"2147483647") });
    QCOMPARE(r.at(1).fields.value(s(u"Track#")), QStringList { s(u"2147483648") });
}

QTEST_GUILESS_MAIN(TagToolsReviewTest)
#include "tst_tagtools_review.moc"
