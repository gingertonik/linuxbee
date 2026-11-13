// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Every expected value is a rule in docs/spec/play-queue.md (Q-x) worked by
// hand. Shuffle is tested by what must always hold (each entry once per
// round, the way back retraces the way there), not by a particular order.
// W-NP is the MusicBee wiki's "Now Playing Preferences" page.
#include <QSignalSpy>
#include <QtTest>

#include "playback/playqueue.h"

using linuxbee::PlayOutcome;
using linuxbee::PlayQueue;

namespace {

QStringList letters(const char *text)
{
    QStringList out;
    for (const char *c = text; *c; ++c)
        out.append(QString(QLatin1Char(*c)));
    return out;
}

// The queue's paths as one word, with the current entry in brackets: "a[b]cd".
QString show(const PlayQueue &q)
{
    QString out;
    const auto entries = q.entries();
    for (int i = 0; i < entries.size(); ++i)
        out += i == q.currentIndex() ? QLatin1Char('[') + entries.at(i).path + QLatin1Char(']') : entries.at(i).path;
    return out;
}

// Calls next() until it says -1 and returns the paths visited, as one word.
QString drain(PlayQueue &q, int atMost = 100)
{
    QString out;
    while (atMost-- > 0 && q.next(false) >= 0)
        out += q.currentPath();
    return out;
}

QString sortedChars(QString text)
{
    std::sort(text.begin(), text.end());
    return text;
}

}

class PlayQueueTest : public QObject {
    Q_OBJECT
private slots:
    void empty();
    void playNowAndStep();
    void repeat();
    void queueing();
    void removing();
    void moving();
    void signalsAndIds();
    void shuffleRound();
    void shuffleBackAndForth();
    void shuffleEdits();
    void classify_data();
    void classify();
};

void PlayQueueTest::empty()
{
    PlayQueue q;
    QCOMPARE(q.count(), 0);
    QCOMPARE(q.currentIndex(), -1);
    QCOMPARE(q.currentPath(), QString());
    QVERIFY(!q.hasNext(false) && !q.hasNext(true));
    QCOMPARE(q.next(false), -1);
    QCOMPARE(q.next(true), -1);
    QCOMPARE(q.previous(), -1);
    QVERIFY(!q.setCurrent(0));
    QVERIFY(!q.move(0, 0));
    QCOMPARE(q.removeAt({ 0 }), 0);
    QVERIFY(q.repeat() == PlayQueue::Repeat::Off);
    QVERIFY(!q.shuffle());
    q.setRepeat(PlayQueue::Repeat::All);
    q.setShuffle(true, 1);
    QCOMPARE(q.next(false), -1); // still nothing to play
}

void PlayQueueTest::playNowAndStep()
{
    // Q-EDIT playNow, Q-NEXT in order
    PlayQueue q;
    q.playNow(letters("abcd"), 1);
    QCOMPARE(show(q), QStringLiteral("a[b]cd"));
    QCOMPARE(q.currentPath(), QStringLiteral("b"));
    QVERIFY(q.hasNext(false));
    QCOMPARE(q.next(false), 2);
    QCOMPARE(q.next(true), 3);
    QVERIFY(!q.hasNext(false));
    QCOMPARE(q.next(false), -1); // the end: the current entry stays
    QCOMPARE(show(q), QStringLiteral("abc[d]"));
    QCOMPARE(q.previous(), 2);
    QCOMPARE(q.previous(), 1);
    QCOMPARE(q.previous(), 0);
    QCOMPARE(q.previous(), -1);
    QCOMPARE(show(q), QStringLiteral("[a]bcd"));
    QVERIFY(q.setCurrent(3));
    QCOMPARE(show(q), QStringLiteral("abc[d]"));
    QVERIFY(!q.setCurrent(4));
    QVERIFY(!q.setCurrent(-1));
    QCOMPARE(show(q), QStringLiteral("abc[d]"));
    // playNow replaces the list. A start outside it means the first entry.
    q.playNow(letters("xy"), 5);
    QCOMPARE(show(q), QStringLiteral("[x]y"));
    q.playNow(letters("xy"), -3);
    QCOMPARE(show(q), QStringLiteral("[x]y"));
    q.playNow({ });
    QCOMPARE(q.count(), 0);
    QCOMPARE(q.currentIndex(), -1);
    // The same path twice is two entries.
    q.playNow(letters("aa"), 1);
    QCOMPARE(q.count(), 2);
    QCOMPARE(q.currentIndex(), 1);
}

void PlayQueueTest::repeat()
{
    PlayQueue q;
    q.playNow(letters("abc"), 2);
    // Q-NEXT Repeat::All wraps both ways.
    q.setRepeat(PlayQueue::Repeat::All);
    QVERIFY(q.hasNext(false));
    QCOMPARE(q.next(true), 0);
    QCOMPARE(q.previous(), 2);
    // Repeat::One: a track that ends by itself plays again; the user's own Next moves on.
    q.setRepeat(PlayQueue::Repeat::One);
    QSignalSpy current(&q, &PlayQueue::currentChanged);
    QVERIFY(q.hasNext(true));
    QCOMPARE(q.next(true), 2);
    QCOMPARE(q.next(true), 2);
    QCOMPARE(current.size(), 0); // the same entry again is not a change
    QVERIFY(!q.hasNext(false)); // c is last, and One does not wrap
    QCOMPARE(q.next(false), -1);
    QVERIFY(q.setCurrent(0));
    QCOMPARE(q.next(false), 1);
    QCOMPARE(q.previous(), 0);
    QCOMPARE(q.previous(), -1);
    q.setRepeat(PlayQueue::Repeat::Off);
    QCOMPARE(q.next(true), 1);
    // One entry, repeat all: Next plays it again.
    q.playNow(letters("z"));
    q.setRepeat(PlayQueue::Repeat::All);
    QCOMPARE(q.next(false), 0);
    QCOMPARE(q.previous(), 0);
}

void PlayQueueTest::queueing()
{
    // Q-EDIT queueNext: right after the current entry, in the order given.
    PlayQueue q;
    q.playNow(letters("abcd"), 1);
    q.queueNext(letters("xy"));
    QCOMPARE(show(q), QStringLiteral("a[b]xycd"));
    q.queueNext(letters("z")); // again: before the earlier ones
    QCOMPARE(show(q), QStringLiteral("a[b]zxycd"));
    q.queueLast(letters("pq"));
    QCOMPARE(show(q), QStringLiteral("a[b]zxycdpq"));
    QCOMPARE(drain(q), QStringLiteral("zxycdpq"));
    q.queueNext({ });
    q.queueLast({ });
    QCOMPARE(q.count(), 9);
    // Into an empty queue: nothing becomes current by itself; next() starts at the top.
    PlayQueue fresh;
    fresh.queueLast(letters("ab"));
    fresh.queueNext(letters("c"));
    QCOMPARE(show(fresh), QStringLiteral("cab"));
    QCOMPARE(fresh.currentIndex(), -1);
    QVERIFY(fresh.hasNext(true));
    QCOMPARE(fresh.next(true), 0);
    QCOMPARE(show(fresh), QStringLiteral("[c]ab"));
}

void PlayQueueTest::removing()
{
    PlayQueue q;
    q.playNow(letters("abcde"), 2);
    // Q-EDIT removeAt: the current entry keeps its place in the music.
    QCOMPARE(q.removeAt({ 0, 4, 9, -1, 0 }), 2);
    QCOMPARE(show(q), QStringLiteral("b[c]d"));
    QCOMPARE(q.removeAt({ }), 0);
    QCOMPARE(q.removeAt({ 7 }), 0);
    // Removing the current entry: nothing is current, and Next goes to the entry that followed it.
    q.playNow(letters("abcde"), 2);
    QCOMPARE(q.removeAt({ 2 }), 1);
    QCOMPARE(show(q), QStringLiteral("abde"));
    QCOMPARE(q.currentIndex(), -1);
    QVERIFY(q.hasNext(true));
    QCOMPARE(q.next(true), 2);
    QCOMPARE(show(q), QStringLiteral("ab[d]e"));
    // ... also when entries before it go at the same time, or afterwards.
    q.playNow(letters("abcde"), 2);
    QCOMPARE(q.removeAt({ 0, 2 }), 2);
    QCOMPARE(show(q), QStringLiteral("bde"));
    QCOMPARE(q.removeAt({ 0 }), 1);
    QCOMPARE(show(q), QStringLiteral("de"));
    QCOMPARE(q.next(false), 0);
    QCOMPARE(show(q), QStringLiteral("[d]e"));
    // ... and Previous goes to the entry that came before it.
    q.playNow(letters("abcde"), 2);
    q.removeAt({ 2 });
    QCOMPARE(q.previous(), 1);
    QCOMPARE(show(q), QStringLiteral("a[b]de"));
    // Queue Next with nothing current puts the tracks where Next will go.
    q.playNow(letters("abcde"), 2);
    q.removeAt({ 2 });
    q.queueNext(letters("x"));
    QCOMPARE(show(q), QStringLiteral("abxde"));
    QCOMPARE(q.next(false), 2);
    // The last entry removed while current: the end, unless repeat wraps.
    q.playNow(letters("ab"), 1);
    q.removeAt({ 1 });
    QVERIFY(!q.hasNext(false));
    QCOMPARE(q.next(false), -1);
    q.setRepeat(PlayQueue::Repeat::All);
    QCOMPARE(q.next(false), 0);
    // clear
    q.clear();
    QCOMPARE(q.count(), 0);
    QCOMPARE(q.currentIndex(), -1);
    QCOMPARE(q.next(false), -1);
}

void PlayQueueTest::moving()
{
    // Q-EDIT move: the entry ends up at the position given; the current entry is followed.
    PlayQueue q;
    q.playNow(letters("abcd"), 1);
    QVERIFY(q.move(1, 3));
    QCOMPARE(show(q), QStringLiteral("acd[b]"));
    QVERIFY(q.move(0, 2));
    QCOMPARE(show(q), QStringLiteral("cda[b]"));
    QVERIFY(q.move(3, 0));
    QCOMPARE(show(q), QStringLiteral("[b]cda"));
    QVERIFY(q.move(2, 1));
    QCOMPARE(show(q), QStringLiteral("[b]dca"));
    QVERIFY(q.move(2, 2));
    QVERIFY(!q.move(0, 4));
    QVERIFY(!q.move(4, 0));
    QVERIFY(!q.move(-1, 0));
    QCOMPARE(show(q), QStringLiteral("[b]dca"));
    QCOMPARE(q.next(false), 1); // Next follows the new order
    QCOMPARE(q.currentPath(), QStringLiteral("d"));
}

void PlayQueueTest::signalsAndIds()
{
    PlayQueue q;
    QSignalSpy entries(&q, &PlayQueue::entriesChanged);
    QSignalSpy current(&q, &PlayQueue::currentChanged);
    QSignalSpy mode(&q, &PlayQueue::modeChanged);
    q.playNow(letters("abc"), 0);
    QCOMPARE(entries.size(), 1);
    QCOMPARE(current.size(), 1);
    QCOMPARE(current.last().at(0).toInt(), 0);
    q.playNow(letters("xyz"), 0); // a new list at the same index is a new current entry
    QCOMPARE(entries.size(), 2);
    QCOMPARE(current.size(), 2);
    q.next(false);
    QCOMPARE(current.size(), 3);
    QCOMPARE(current.last().at(0).toInt(), 1);
    QCOMPARE(entries.size(), 2);
    q.setCurrent(1); // already there
    QCOMPARE(current.size(), 3);
    q.queueNext(letters("q")); // x[y]qz: the current index did not change
    QCOMPARE(entries.size(), 3);
    QCOMPARE(current.size(), 3);
    q.removeAt({ 0 }); // [y]qz: it did now
    QCOMPARE(entries.size(), 4);
    QCOMPARE(current.size(), 4);
    QCOMPARE(current.last().at(0).toInt(), 0);
    q.move(0, 0);
    q.removeAt({ 9 });
    q.queueLast({ });
    QCOMPARE(entries.size(), 4);
    q.setRepeat(PlayQueue::Repeat::All);
    q.setRepeat(PlayQueue::Repeat::All);
    q.setShuffle(true, 3);
    q.setShuffle(true, 4); // on already: a new seed, not a new mode
    QCOMPARE(mode.size(), 2);
    q.clear();
    QCOMPARE(entries.size(), 5);
    QCOMPARE(current.last().at(0).toInt(), -1);
    q.clear();
    QCOMPARE(entries.size(), 5);

    // Entry ids are unique, also for the same path, and stay with their entry.
    q.setShuffle(false, 0);
    q.playNow(letters("aab"), 0);
    q.queueLast(letters("a"));
    auto ids = [&q] {
        QList<qint64> out;
        for (const auto &e : q.entries())
            out.append(e.entryId);
        return out;
    };
    const QList<qint64> before = ids();
    QCOMPARE(QSet<qint64>(before.begin(), before.end()).size(), 4);
    q.move(0, 3);
    QCOMPARE(ids(), QList<qint64>({ before.at(1), before.at(2), before.at(3), before.at(0) }));
    q.removeAt({ 0 });
    q.queueLast(letters("c"));
    const QList<qint64> after = ids();
    QCOMPARE(after.size(), 4);
    QVERIFY(!before.contains(after.last())); // an id is not handed out twice
}

void PlayQueueTest::shuffleRound()
{
    // Q-SHUFFLE: a round plays every other entry exactly once.
    int notInOrder = 0;
    for (quint32 seed = 1; seed <= 10; ++seed) {
        PlayQueue q;
        q.playNow(letters("abcdefghij"), 0);
        q.setShuffle(true, seed);
        QVERIFY(q.shuffle());
        QCOMPARE(q.currentIndex(), 0); // switching shuffle on does not change what is playing
        const QString round = drain(q);
        QCOMPARE(sortedChars(round), QStringLiteral("bcdefghij"));
        notInOrder += round != QStringLiteral("bcdefghij") ? 1 : 0;
        QVERIFY(!q.hasNext(false));
        QCOMPARE(q.next(false), -1);
        // The same seed gives the same round.
        PlayQueue again;
        again.playNow(letters("abcdefghij"), 0);
        again.setShuffle(true, seed);
        QCOMPARE(drain(again), round);
    }
    QVERIFY(notInOrder >= 9);
    // Repeat::All: when a round is over, another begins: every entry but the current one, once.
    PlayQueue q;
    q.playNow(letters("abcde"), 2);
    q.setShuffle(true, 11);
    q.setRepeat(PlayQueue::Repeat::All);
    QString first, second;
    for (int i = 0; i < 4; ++i) {
        QVERIFY(q.next(true) >= 0);
        first += q.currentPath();
    }
    QCOMPARE(sortedChars(first), QStringLiteral("abde"));
    const QString last = q.currentPath();
    for (int i = 0; i < 4; ++i) {
        QVERIFY(q.hasNext(true));
        QVERIFY(q.next(true) >= 0);
        second += q.currentPath();
    }
    QCOMPARE(sortedChars(second), sortedChars(QStringLiteral("abcde").remove(last)));
    // Repeat::One still repeats; one entry with repeat all stays.
    q.setRepeat(PlayQueue::Repeat::One);
    const int here = q.currentIndex();
    QCOMPARE(q.next(true), here);
    PlayQueue one;
    one.playNow(letters("z"));
    one.setShuffle(true, 5);
    QCOMPARE(one.next(false), -1);
    one.setRepeat(PlayQueue::Repeat::All);
    QCOMPARE(one.next(false), 0);
    // Shuffle off: on from here in list order.
    PlayQueue off;
    off.playNow(letters("abcde"), 1);
    off.setShuffle(true, 9);
    off.setShuffle(false, 0);
    QVERIFY(!off.shuffle());
    QCOMPARE(drain(off), QStringLiteral("cde"));
}

void PlayQueueTest::shuffleBackAndForth()
{
    // Q-SHUFFLE: Previous goes back through what was played; Next then retraces it.
    PlayQueue q;
    q.playNow(letters("abcdefgh"), 0);
    q.setShuffle(true, 21);
    QCOMPARE(q.previous(), -1); // nothing was played before
    QString there;
    for (int i = 0; i < 3; ++i) {
        q.next(false);
        there += q.currentPath();
    }
    QString back;
    back += q.currentPath();
    while (q.previous() >= 0)
        back += q.currentPath();
    QString expectedBack = QLatin1Char('a') + there;
    std::reverse(expectedBack.begin(), expectedBack.end());
    QCOMPARE(back, expectedBack); // the 3rd, back through the 2nd and the 1st, then a
    QCOMPARE(q.currentPath(), QStringLiteral("a"));
    QString again;
    for (int i = 0; i < 3; ++i) {
        q.next(false);
        again += q.currentPath();
    }
    QCOMPARE(again, there);
    // And the round still plays each remaining entry once.
    QCOMPARE(sortedChars(there + drain(q)), QStringLiteral("bcdefgh"));
}

void PlayQueueTest::shuffleEdits()
{
    // Queue Next means next, shuffle or not.
    PlayQueue q;
    q.playNow(letters("abcdef"), 0);
    q.setShuffle(true, 4);
    q.next(false);
    const QString playing = q.currentPath();
    q.queueNext(letters("xy"));
    QCOMPARE(q.count(), 8);
    QVERIFY(q.currentIndex() >= 0 && q.currentIndex() + 1 < q.count());
    QCOMPARE(q.entries().at(q.currentIndex() + 1).path, QStringLiteral("x"));
    q.next(false);
    QCOMPARE(q.currentPath(), QStringLiteral("x"));
    q.next(false);
    QCOMPARE(q.currentPath(), QStringLiteral("y"));
    // Queue Last joins the round: played once, somewhere.
    q.queueLast(letters("z"));
    const QString rest = drain(q);
    QCOMPARE(sortedChars(playing + rest), QStringLiteral("bcdefz"));
    // A removed entry is not played; a jump is not played twice, and Previous returns from it.
    PlayQueue r;
    r.playNow(letters("abcdef"), 0);
    r.setShuffle(true, 8);
    r.removeAt({ 3 }); // d
    QVERIFY(r.setCurrent(4)); // f, in the list abcef
    QCOMPARE(r.currentPath(), QStringLiteral("f"));
    const QString after = drain(r);
    QCOMPARE(sortedChars(after), QStringLiteral("bce"));
    while (r.currentPath() != QStringLiteral("f"))
        QVERIFY(r.previous() >= 0);
    QCOMPARE(r.previous(), 0); // back to a, where the jump was made from
    QCOMPARE(r.previous(), -1);
    // playNow under shuffle starts a new round with the new list.
    r.playNow(letters("mnop"), 3);
    QCOMPARE(r.currentPath(), QStringLiteral("p"));
    QCOMPARE(sortedChars(drain(r)), QStringLiteral("mno"));
}

void PlayQueueTest::classify_data()
{
    QTest::addColumn<qint64>("listened");
    QTest::addColumn<qint64>("duration");
    QTest::addColumn<bool>("byUser");
    QTest::addColumn<int>("expected");
    auto row = [](const char *name, qint64 listened, qint64 duration, bool byUser, PlayOutcome expected) {
        QTest::newRow(name) << listened << duration << byUser << int(expected);
    };
    // Q-COUNT with the default rule (W-NP: a play is more than 50% or more than
    // 4 minutes; a skip is less than 20 seconds)
    row("over-half", 150001, 300000, false, PlayOutcome::Played);
    row("exactly-half", 150000, 300000, false, PlayOutcome::Neither);
    row("over-half-then-skipped", 200000, 300000, true, PlayOutcome::Played);
    row("over-four-minutes", 240001, 1000000, true, PlayOutcome::Played);
    row("exactly-four-minutes", 240000, 1000000, false, PlayOutcome::Neither);
    row("skipped-early", 5000, 300000, true, PlayOutcome::Skipped);
    row("ended-early-by-itself", 5000, 300000, false, PlayOutcome::Neither); // not the user's doing
    row("just-under-skip", 19999, 300000, true, PlayOutcome::Skipped);
    row("at-skip-limit", 20000, 300000, true, PlayOutcome::Neither);
    row("middle", 100000, 300000, true, PlayOutcome::Neither);
    row("short-track-half", 16000, 30000, true, PlayOutcome::Played); // a play wins over a skip
    row("nothing", 0, 300000, true, PlayOutcome::Skipped);
    row("negative", -500, 300000, true, PlayOutcome::Skipped);
    row("unknown-length", 100000, 0, false, PlayOutcome::Neither); // only the time rule can apply
    row("unknown-length-long", 240001, 0, false, PlayOutcome::Played);
    row("unknown-length-skip", 1000, 0, true, PlayOutcome::Skipped);
}

void PlayQueueTest::classify()
{
    QFETCH(qint64, listened);
    QFETCH(qint64, duration);
    QFETCH(bool, byUser);
    QFETCH(int, expected);
    QCOMPARE(int(linuxbee::classifyPlay(listened, duration, byUser)), expected);
    if (QTest::currentDataTag() == QByteArray("middle")) {
        // The rule is the caller's to set.
        linuxbee::PlayCountRule rule;
        rule.playedFraction = 0.25;
        rule.playedSeconds = 600;
        rule.skipSeconds = 120;
        QVERIFY(linuxbee::classifyPlay(100000, 300000, true, rule) == PlayOutcome::Played);
        QVERIFY(linuxbee::classifyPlay(70000, 300000, true, rule) == PlayOutcome::Skipped);
        QVERIFY(linuxbee::classifyPlay(70000, 300000, false, rule) == PlayOutcome::Neither);
        const linuxbee::PlayCountRule defaults;
        QVERIFY(defaults.playedFraction == 0.5 && defaults.playedSeconds == 240 && defaults.skipSeconds == 20);
    }
}

QTEST_APPLESS_MAIN(PlayQueueTest)
#include "tst_playqueue.moc"
