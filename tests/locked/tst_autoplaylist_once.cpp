// LOCKED ACCEPTANCE TEST (an auto-playlist checks its rules once, not once for each track). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/auto-playlists.md (A-MATCH, "Speed")
// and docs/spec/compiled-rules.md (RC-COUNT). The cost is counted, not timed.
#include <QtTest>

#include "playlists/autoplaylist.h"
#include "rules/compiledrules.h"

using namespace linuxbee;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

QList<LibraryTrack> library(int n)
{
    QList<LibraryTrack> out;
    for (int i = 0; i < n; ++i) {
        LibraryTrack t;
        t.id = i + 1;
        t.path = QStringLiteral("/m/%1.flac").arg(i);
        t.category = s(u"Music");
        t.tags.fields.insert(s(u"Title"), { QStringLiteral("Song %1").arg(i) });
        t.tags.fields.insert(s(u"Artist"), { QStringLiteral("Artist %1").arg(i % 7) });
        t.missing = i % 50 == 49;
        out.append(t);
    }
    return out;
}

Rule rule(const char16_t *field, RuleOperator op, const QStringList &values)
{
    Rule r;
    r.field = s(field);
    r.op = op;
    r.values = values;
    return r;
}

RuleContext now()
{
    RuleContext c;
    c.now = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);
    return c;
}

}

class AutoPlaylistOnceTest : public QObject {
    Q_OBJECT
private slots:
    void theCounter();
    void onceForAllTracks();
    void theAnswersAreTheSame();
};

void AutoPlaylistOnceTest::theCounter()
{
    // RC-COUNT: one for each object made from a set, one for each call of matchesRules; none for asking or copying.
    RuleSet set;
    set.rules = { rule(u"Title", RuleOperator::Matches, { s(u"^song [0-9]+$") }) };
    const QList<LibraryTrack> tracks = library(3);
    qint64 at = ruleSetsChecked();
    const CompiledRules ready(set);
    QCOMPARE(ruleSetsChecked() - at, 1);
    at = ruleSetsChecked();
    const CompiledRules copy = ready;
    for (const LibraryTrack &t : tracks)
        QVERIFY(copy.matches(LibraryFieldSource(t), now()));
    const CompiledRules empty;
    QVERIFY(!empty.isValid());
    QCOMPARE(ruleSetsChecked() - at, 0);
    for (const LibraryTrack &t : tracks)
        QVERIFY(matchesRules(set, LibraryFieldSource(t), now()));
    QCOMPARE(ruleSetsChecked() - at, 3);
    // A set that cannot be used is checked all the same: that is how it is found out.
    RuleSet bad;
    bad.rules = { rule(u"Title", RuleOperator::Matches, { s(u"(") }) };
    at = ruleSetsChecked();
    QVERIFY(!CompiledRules(bad).isValid());
    QVERIFY(!matchesRules(bad, LibraryFieldSource(tracks.at(0)), now()));
    QCOMPARE(ruleSetsChecked() - at, 2);
}

void AutoPlaylistOnceTest::onceForAllTracks()
{
    // A-MATCH, speed: the rules are checked once for an evaluation, however many tracks there are.
    AutoPlaylist p;
    p.rules.rules = { rule(u"Title", RuleOperator::Matches, { s(u"[05]$") }), rule(u"Artist", RuleOperator::IsNot, { s(u"Artist 3") }) };
    for (const int n : { 0, 10, 2000 }) {
        const QList<LibraryTrack> tracks = library(n);
        const qint64 at = ruleSetsChecked();
        const QList<qint64> ids = evaluateAutoPlaylist(p, tracks, now(), 1);
        QVERIFY2(ruleSetsChecked() - at == 1, qPrintable(QStringLiteral("%1 tracks: %2").arg(n).arg(ruleSetsChecked() - at)));
        QVERIFY(n == 0 || !ids.isEmpty());
    }
    // With a limit, duplicates filtered and an order: still once.
    p.limitEnabled = true;
    p.limitAmount = 5;
    p.filterDuplicates = true;
    p.selectedBy = AutoPlaylist::SelectedBy::MostRecentlyAdded;
    qint64 at = ruleSetsChecked();
    QCOMPARE(evaluateAutoPlaylist(p, library(500), now(), 1).size(), 5);
    QCOMPARE(ruleSetsChecked() - at, 1);
    // A set that cannot be used: once, and nothing matches.
    AutoPlaylist bad;
    bad.rules.rules = { rule(u"Title", RuleOperator::Matches, { s(u"(") }) };
    at = ruleSetsChecked();
    QCOMPARE(evaluateAutoPlaylist(bad, library(500), now(), 1), QList<qint64>());
    QCOMPARE(ruleSetsChecked() - at, 1);
}

void AutoPlaylistOnceTest::theAnswersAreTheSame()
{
    // A-MATCH: the tracks are those matchesRules says, no missing one among them.
    const QList<LibraryTrack> tracks = library(600);
    QList<RuleSet> sets;
    RuleSet one;
    one.rules = { rule(u"Title", RuleOperator::Matches, { s(u"1[0-9]$") }) };
    sets.append(one);
    RuleSet any;
    any.match = RuleSet::Match::Any;
    any.rules = { rule(u"Artist", RuleOperator::Is, { s(u"artist 2") }), rule(u"Title", RuleOperator::EndsWith, { s(u"77") }), rule(u"Title", RuleOperator::DoesNotMatch, { s(u"[0-8]$") }) };
    sets.append(any);
    sets.append(RuleSet()); // no rules: every track
    for (const RuleSet &set : sets) {
        AutoPlaylist p;
        p.rules = set;
        p.selectedBy = AutoPlaylist::SelectedBy::LeastRecentlyAdded;
        QList<qint64> want;
        for (const LibraryTrack &t : tracks)
            if (!t.missing && matchesRules(set, LibraryFieldSource(t), now()))
                want.append(t.id);
        QList<qint64> got = evaluateAutoPlaylist(p, tracks, now(), 1);
        std::sort(got.begin(), got.end());
        QVERIFY(!want.isEmpty() && want.size() < tracks.size());
        QCOMPARE(got, want);
    }
}

QTEST_GUILESS_MAIN(AutoPlaylistOnceTest)
#include "tst_autoplaylist_once.moc"
