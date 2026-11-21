// LOCKED ACCEPTANCE TEST (the play queue: Queue Next goes after the earlier ones). Written by
// the reviewer, not the builder. The builder makes these pass by changing src/, never by editing
// this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Rules: docs/spec/play-queue.md, Q-EDIT and Q-SHUFFLE.
//   W-NP  MusicBee wiki, "Now Playing Preferences": Queue Next places the tracks "after any
//         others that have been added with the same command"
//   W-MPV MusicBee wiki, "Main Panel Views": the same
//   Q-x   a linuxBee decision in play-queue.md (the public pages are silent)
#include <QtTest>

#include "playback/playqueue.h"

using linuxbee::PlayQueue;

namespace {

QStringList letters(const char *text)
{
    QStringList out;
    for (const char *c = text; *c; ++c)
        out.append(QString(QLatin1Char(*c)));
    return out;
}

// The paths in order, the current entry in brackets.
QString show(const PlayQueue &q)
{
    QString out;
    const QList<PlayQueue::Entry> list = q.entries();
    for (int i = 0; i < list.size(); ++i)
        out += i == q.currentIndex() ? QStringLiteral("[%1]").arg(list.at(i).path)
                                     : list.at(i).path;
    return out;
}

}

class PlayQueueNextOrderTest : public QObject {
    Q_OBJECT
private slots:
    void afterTheEarlierOnes();
    void stillWaitingAfterOneIsReached();
    void playedOnesAreNotSkipped();
    void aNewListForgets();
    void queueLastIsNotAQueueNext();
    void removedAndMoved();
    void withNothingCurrent();
    void intoAnEmptyQueue();
    void underShuffle();
};

void PlayQueueNextOrderTest::afterTheEarlierOnes()
{
    // W-NP, W-MPV.
    PlayQueue q;
    q.playNow(letters("abcd"), 1);
    q.queueNext(letters("xy"));
    q.queueNext(letters("z"));
    QCOMPARE(show(q), QStringLiteral("a[b]xyzcd"));
    q.queueNext(letters("w"));
    QCOMPARE(show(q), QStringLiteral("a[b]xyzwcd"));
    QCOMPARE(q.currentIndex(), 1);
}

void PlayQueueNextOrderTest::stillWaitingAfterOneIsReached()
{
    // Q-EDIT: the ones not yet played still count.
    PlayQueue q;
    q.playNow(letters("abcd"), 1);
    q.queueNext(letters("xyz"));
    QCOMPARE(q.next(false), 2);
    q.queueNext(letters("q"));
    QCOMPARE(show(q), QStringLiteral("ab[x]yzqcd"));
}

void PlayQueueNextOrderTest::playedOnesAreNotSkipped()
{
    // Q-EDIT: a track that has been current is no longer waiting, even after Previous.
    PlayQueue q;
    q.playNow(letters("abcd"), 1);
    q.queueNext(letters("xy"));
    QCOMPARE(q.next(false), 2);
    QCOMPARE(q.previous(), 1);
    q.queueNext(letters("q"));
    QCOMPARE(show(q), QStringLiteral("a[b]qxycd"));
    // A jump elsewhere: the run is no longer next to the current entry.
    PlayQueue j;
    j.playNow(letters("abcde"), 0);
    j.queueNext(letters("xy"));
    QVERIFY(j.setCurrent(5));
    j.queueNext(letters("q"));
    QCOMPARE(show(j), QStringLiteral("axybc[d]qe"));
}

void PlayQueueNextOrderTest::aNewListForgets()
{
    // A new list has new entries; nothing of the old one waits.
    PlayQueue q;
    q.playNow(letters("abc"), 0);
    q.queueNext(letters("x"));
    q.playNow(letters("abc"), 0);
    q.queueNext(letters("y"));
    q.queueNext(letters("z"));
    QCOMPARE(show(q), QStringLiteral("[a]yzbc"));
    q.clear();
    q.playNow(letters("ab"), 0);
    q.queueNext(letters("w"));
    QCOMPARE(show(q), QStringLiteral("[a]wb"));
}

void PlayQueueNextOrderTest::queueLastIsNotAQueueNext()
{
    PlayQueue q;
    q.playNow(letters("ab"), 0);
    q.queueLast(letters("z"));
    q.queueNext(letters("x"));
    q.queueNext(letters("y"));
    QCOMPARE(show(q), QStringLiteral("[a]xybz"));
    PlayQueue end;
    end.playNow(letters("a"), 0);
    end.queueLast(letters("z"));
    end.queueNext(letters("x"));
    QCOMPARE(show(end), QStringLiteral("[a]xz")); // z is after a, but not a Queue Next
}

void PlayQueueNextOrderTest::removedAndMoved()
{
    // Q-EDIT: the waiting ones that directly follow the current entry.
    PlayQueue q;
    q.playNow(letters("abc"), 0);
    q.queueNext(letters("xy"));
    q.removeAt({ 1 });
    q.queueNext(letters("z"));
    QCOMPARE(show(q), QStringLiteral("[a]yzbc"));
    PlayQueue m;
    m.playNow(letters("abc"), 0);
    m.queueNext(letters("xy"));
    QVERIFY(m.move(1, 4)); // [a]ybcx
    m.queueNext(letters("z"));
    QCOMPARE(show(m), QStringLiteral("[a]yzbcx"));
}

void PlayQueueNextOrderTest::withNothingCurrent()
{
    // Q-EDIT: at the remembered place, after the earlier ones there.
    PlayQueue q;
    q.playNow(letters("abcde"), 2);
    q.removeAt({ 2 });
    q.queueNext(letters("x"));
    q.queueNext(letters("y"));
    QCOMPARE(show(q), QStringLiteral("abxyde"));
    QCOMPARE(q.next(false), 2);
    QCOMPARE(q.currentPath(), QStringLiteral("x"));
}

void PlayQueueNextOrderTest::intoAnEmptyQueue()
{
    PlayQueue q;
    q.queueLast(letters("ab"));
    q.queueNext(letters("c"));
    q.queueNext(letters("d"));
    QCOMPARE(show(q), QStringLiteral("cdab"));
    QCOMPARE(q.currentIndex(), -1);
    PlayQueue none;
    none.queueNext(letters("x"));
    none.queueNext(letters("y"));
    QCOMPARE(show(none), QStringLiteral("xy"));
}

void PlayQueueNextOrderTest::underShuffle()
{
    // Q-SHUFFLE: played next, in the order they were queued, before the rest of the round.
    PlayQueue q;
    q.playNow(letters("abcdef"), 0);
    q.setShuffle(true, 4);
    q.next(false);
    q.queueNext(letters("x"));
    q.queueNext(letters("yz"));
    QCOMPARE(q.count(), 9);
    for (const char *want : { "x", "y", "z" }) {
        q.next(false);
        QCOMPARE(q.currentPath(), QString::fromLatin1(want));
    }
    q.queueNext(letters("p"));
    q.next(false);
    QCOMPARE(q.currentPath(), QStringLiteral("p"));
}

QTEST_GUILESS_MAIN(PlayQueueNextOrderTest)
#include "tst_playqueue_next_order.moc"
