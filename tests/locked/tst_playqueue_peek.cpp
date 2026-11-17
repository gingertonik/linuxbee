// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The queue says what Next would give, without moving. Expected values: docs/spec/play-queue.md,
// Q-PEEK. The long cases walk many made-up runs of calls; the numbers behind them come from a
// generator written here, so the runs are the same on every machine.
#include <QSignalSpy>
#include <QtTest>

#include <optional>

#include "playback/playqueue.h"

using namespace linuxbee;

namespace {

using Peek = std::optional<PlayQueue::Entry>;

QStringList paths(const char *letters)
{
    QStringList out;
    for (const char *c = letters; *c; ++c)
        out.append(QStringLiteral("/m/%1.flac").arg(QLatin1Char(*c)));
    return out;
}

bool same(const Peek &a, const Peek &b)
{
    if (a.has_value() != b.has_value())
        return false;
    return !a.has_value() || (a->entryId == b->entryId && a->path == b->path);
}

QString text(const Peek &p)
{
    return p.has_value() ? QStringLiteral("%1#%2").arg(p->path).arg(p->entryId)
                         : QStringLiteral("(none)");
}

QString state(const PlayQueue &q)
{
    QString out = QStringLiteral("cur=%1 rep=%2 shuf=%3:")
                      .arg(q.currentIndex())
                      .arg(int(q.repeat()))
                      .arg(q.shuffle());
    for (const PlayQueue::Entry &e : q.entries())
        out += QStringLiteral(" %1#%2").arg(e.path.mid(3, 1)).arg(e.entryId);
    return out;
}

// A small generator of its own, so that a run is the same everywhere.
struct Dice {
    quint32 x;
    int roll(int below)
    {
        x = x * 1664525u + 1013904223u;
        return int((x >> 8) % quint32(below));
    }
};

// One made-up call on the queue. The same dice give the same call on a twin.
QString act(PlayQueue &q, Dice &dice)
{
    static const char *const kLists[] = { "abcde", "a", "ab", "abcdefghij", "", "aab" };
    const int n = q.count();
    switch (dice.roll(15)) {
    case 0: {
        const int list = dice.roll(6);
        const int at = dice.roll(4);
        q.playNow(paths(kLists[list]), at);
        return QStringLiteral("playNow %1 %2").arg(QLatin1String(kLists[list])).arg(at);
    }
    case 1:
        q.queueNext(paths(dice.roll(2) ? "x" : "xy"));
        return QStringLiteral("queueNext");
    case 2:
        q.queueLast(paths(dice.roll(2) ? "z" : "zw"));
        return QStringLiteral("queueLast");
    case 3: {
        const int at = dice.roll(n + 1);
        q.removeAt({ at });
        return QStringLiteral("removeAt %1").arg(at);
    }
    case 4: {
        const int from = dice.roll(n + 1);
        const int to = dice.roll(n + 1);
        q.move(from, to);
        return QStringLiteral("move %1 %2").arg(from).arg(to);
    }
    case 5: {
        const int at = dice.roll(n + 1);
        q.setCurrent(at);
        return QStringLiteral("setCurrent %1").arg(at);
    }
    case 6:
    case 7:
        q.next(true);
        return QStringLiteral("next(true)");
    case 8:
        q.next(false);
        return QStringLiteral("next(false)");
    case 9:
        q.previous();
        return QStringLiteral("previous");
    case 10: {
        const int mode = dice.roll(3);
        q.setRepeat(PlayQueue::Repeat(mode));
        return QStringLiteral("setRepeat %1").arg(mode);
    }
    case 11: {
        const int seed = dice.roll(1000);
        q.setShuffle(true, quint32(seed));
        return QStringLiteral("setShuffle on %1").arg(seed);
    }
    case 12:
        q.setShuffle(false, 0);
        return QStringLiteral("setShuffle off");
    case 13:
        q.removePaths(paths(dice.roll(2) ? "a" : "bx"));
        return QStringLiteral("removePaths");
    default:
        if (dice.roll(8) == 0) {
            q.clear();
            return QStringLiteral("clear");
        }
        q.renamePaths({ { QStringLiteral("/m/c.flac"), QStringLiteral("/m/q.flac") } });
        return QStringLiteral("renamePaths");
    }
}

}

class PlayQueuePeekTest : public QObject {
    Q_OBJECT
private slots:
    void anEmptyQueue();
    void inListOrder();
    void repeatOneIsForATrackThatEnded();
    void withNoCurrentEntry();
    void alongAShuffleRound();
    void whenTheRoundIsUsedUp();
    void oneEntryThatRepeats();
    void aNoteWhenTheAnswerMayHaveChanged();
    void noNoteWhenNothingChanged();
    void itIsWhatNextGives_data();
    void itIsWhatNextGives();
    void lookingChangesNothing_data();
    void lookingChangesNothing();
    void aChangedAnswerIsAlwaysNoted_data();
    void aChangedAnswerIsAlwaysNoted();
};

void PlayQueuePeekTest::anEmptyQueue()
{
    PlayQueue q;
    QVERIFY(!q.peekNext(true).has_value());
    QVERIFY(!q.peekNext(false).has_value());
    q.setRepeat(PlayQueue::Repeat::All);
    q.setShuffle(true, 3);
    QVERIFY(!q.peekNext(true).has_value());
}

void PlayQueuePeekTest::inListOrder()
{
    PlayQueue q;
    q.playNow(paths("abc"), 0);
    const QList<PlayQueue::Entry> entries = q.entries();
    QCOMPARE(text(q.peekNext(true)), text(entries.at(1)));
    QCOMPARE(text(q.peekNext(false)), text(entries.at(1)));
    QCOMPARE(q.currentIndex(), 0); // looking did not move it
    q.setCurrent(2);
    QVERIFY(!q.peekNext(true).has_value()); // the end of the list
    QVERIFY(!q.peekNext(false).has_value());
    q.setRepeat(PlayQueue::Repeat::All);
    QCOMPARE(text(q.peekNext(true)), text(entries.at(0)));
    QCOMPARE(text(q.peekNext(false)), text(entries.at(0)));
    // The same path twice is two entries: the id tells them apart.
    q.playNow(paths("aa"), 0);
    QCOMPARE(q.peekNext(true)->entryId, q.entries().at(1).entryId);
}

void PlayQueuePeekTest::repeatOneIsForATrackThatEnded()
{
    PlayQueue q;
    q.playNow(paths("abc"), 1);
    q.setRepeat(PlayQueue::Repeat::One);
    const QList<PlayQueue::Entry> entries = q.entries();
    QCOMPARE(text(q.peekNext(true)), text(entries.at(1))); // it joins to itself
    QCOMPARE(text(q.peekNext(false)), text(entries.at(2))); // the user's own Next goes on
    q.setCurrent(2);
    QCOMPARE(text(q.peekNext(true)), text(entries.at(2)));
    QVERIFY(!q.peekNext(false).has_value());
}

void PlayQueuePeekTest::withNoCurrentEntry()
{
    PlayQueue q;
    q.playNow(paths("abcd"), 1);
    q.removeAt({ 1 }); // nothing is current; the queue remembers the place
    QCOMPARE(q.currentIndex(), -1);
    const QList<PlayQueue::Entry> entries = q.entries();
    QCOMPARE(text(q.peekNext(true)), text(entries.at(1))); // "c", which followed the removed one
    q.setRepeat(PlayQueue::Repeat::One); // nothing is current, so nothing repeats
    QCOMPARE(text(q.peekNext(true)), text(entries.at(1)));
    QCOMPARE(q.next(true), 1);
}

void PlayQueuePeekTest::alongAShuffleRound()
{
    PlayQueue q;
    q.playNow(paths("abcdefgh"), 0);
    q.setShuffle(true, 7);
    QStringList seen;
    for (int i = 0; i < 7; ++i) {
        const Peek ahead = q.peekNext(true);
        QVERIFY(ahead.has_value());
        QVERIFY(same(q.peekNext(true), ahead)); // asked twice, the same
        QVERIFY(same(q.peekNext(false), ahead));
        const int index = q.next(true);
        QVERIFY(index >= 0);
        QCOMPARE(text(ahead), text(q.entries().at(index)));
        seen.append(ahead->path);
    }
    QCOMPARE(QSet<QString>(seen.cbegin(), seen.cend()).size(), 7); // a round: each other entry once
    QVERIFY(!q.peekNext(true).has_value()); // used up, and no repeat
    QCOMPARE(q.next(true), -1);
}

// The hard one: the next entry belongs to a round that has not been dealt yet. What is foretold
// must be what the new round then gives, and foretelling must not change how it is dealt.
void PlayQueuePeekTest::whenTheRoundIsUsedUp()
{
    for (quint32 seed = 1; seed <= 40; ++seed) {
        PlayQueue q;
        PlayQueue twin; // never looked at
        for (PlayQueue *each : { &q, &twin }) {
            each->playNow(paths("abcdef"), 2);
            each->setRepeat(PlayQueue::Repeat::All);
            each->setShuffle(true, seed);
            for (int i = 0; i < 5; ++i)
                QVERIFY(each->next(true) >= 0);
        }
        // Three rounds on, looking before every step.
        for (int step = 0; step < 15; ++step) {
            const Peek ahead = q.peekNext(true);
            QVERIFY(ahead.has_value());
            QVERIFY(same(q.peekNext(true), ahead));
            QVERIFY(same(q.peekNext(false), ahead));
            QVERIFY2(ahead->entryId != q.entries().at(q.currentIndex()).entryId,
                "a new round never begins with the entry that is playing");
            const int index = q.next(true);
            QVERIFY(index >= 0);
            QCOMPARE(text(ahead), text(q.entries().at(index)));
            QCOMPARE(twin.next(true), index); // the twin, never looked at, went the same way
        }
    }
}

void PlayQueuePeekTest::oneEntryThatRepeats()
{
    PlayQueue q;
    q.playNow(paths("a"), 0);
    const PlayQueue::Entry only = q.entries().at(0);
    QVERIFY(!q.peekNext(true).has_value());
    q.setRepeat(PlayQueue::Repeat::All);
    QCOMPARE(text(q.peekNext(true)), text(only)); // list order: round to the first, which is itself
    q.setShuffle(true, 5);
    QCOMPARE(text(q.peekNext(true)), text(only)); // Q-SHUFFLE: it stays on its entry
    QCOMPARE(text(q.peekNext(false)), text(only));
    QCOMPARE(q.next(true), 0);
    q.setRepeat(PlayQueue::Repeat::Off);
    QVERIFY(!q.peekNext(true).has_value());
}

void PlayQueuePeekTest::aNoteWhenTheAnswerMayHaveChanged()
{
    PlayQueue q;
    QSignalSpy spy(&q, &PlayQueue::nextChanged);
    const auto noted = [&spy] {
        const bool any = !spy.isEmpty();
        spy.clear();
        return any;
    };
    q.playNow(paths("abcde"), 0);
    QVERIFY(noted());
    q.queueNext(paths("x"));
    QVERIFY(noted());
    q.removeAt({ 1 });
    QVERIFY(noted());
    q.move(1, 3);
    QVERIFY(noted());
    q.setRepeat(PlayQueue::Repeat::One);
    QVERIFY(noted());
    q.next(false);
    QVERIFY(noted());
    q.setShuffle(true, 1);
    QVERIFY(noted());
    // Shuffle switched on again deals a new round. Nothing else says so (Q-SIGNALS has no signal
    // for it).
    q.setRepeat(PlayQueue::Repeat::Off);
    spy.clear();
    QSignalSpy mode(&q, &PlayQueue::modeChanged);
    bool differed = false;
    for (quint32 seed = 2; seed < 12; ++seed) {
        const Peek before = q.peekNext(true);
        q.setShuffle(true, seed);
        QVERIFY2(noted(), "a new round was dealt and nothing said so");
        differed = differed || !same(before, q.peekNext(true));
    }
    QVERIFY(differed); // of ten new rounds one at least began elsewhere
    QCOMPARE(mode.size(), 0);
    q.setShuffle(false, 0);
    QVERIFY(noted());
    q.clear();
    QVERIFY(noted());
}

// Q-SIGNALS: a call that changes nothing emits nothing.
void PlayQueuePeekTest::noNoteWhenNothingChanged()
{
    PlayQueue q;
    q.playNow(paths("abc"), 0);
    QSignalSpy spy(&q, &PlayQueue::nextChanged);
    q.setRepeat(PlayQueue::Repeat::Off);
    q.setShuffle(false, 9);
    q.queueLast({ });
    q.queueNext({ });
    q.removeAt({ 17 });
    q.removePaths(paths("z"));
    q.move(9, 0);
    q.setCurrent(9);
    (void)q.peekNext(true);
    (void)q.peekNext(false);
    (void)q.hasNext(true);
    QCOMPARE(spy.size(), 0);
    q.setCurrent(2);
    QCOMPARE(q.next(true), -1); // nowhere to go: nothing changed
    QCOMPARE(q.next(false), -1);
    spy.clear();
    QCOMPARE(q.next(true), -1);
    QCOMPARE(spy.size(), 0);
}

void PlayQueuePeekTest::itIsWhatNextGives_data()
{
    QTest::addColumn<quint32>("run");
    for (quint32 run = 1; run <= 300; ++run)
        QTest::addRow("run %u", run) << run;
}

// Whatever was done to the queue: what it foretells is where Next then goes, for a track that ended
// and for the user's own Next alike.
void PlayQueuePeekTest::itIsWhatNextGives()
{
    QFETCH(quint32, run);
    Dice dice { run * 2654435761u };
    PlayQueue q;
    QString trail;
    for (int step = 0; step < 80; ++step) {
        trail = act(q, dice);
        const bool automatic = dice.roll(2) == 0;
        const Peek ahead = q.peekNext(automatic);
        const QString where = QStringLiteral("step %1 after %2: %3").arg(step).arg(trail, state(q));
        QVERIFY2(q.hasNext(automatic) == ahead.has_value(), qPrintable(where));
        const int before = q.currentIndex();
        const int index = q.next(automatic);
        if (index < 0) {
            QVERIFY2(
                !ahead.has_value(), qPrintable(where + QStringLiteral(" foretold ") + text(ahead)));
            QCOMPARE(q.currentIndex(), before);
        } else {
            QVERIFY2(ahead.has_value(), qPrintable(where + QStringLiteral(" foretold nothing")));
            QVERIFY2(same(ahead, Peek(q.entries().at(index))),
                qPrintable(where + QStringLiteral(" foretold ") + text(ahead)
                    + QStringLiteral(" got ") + text(q.entries().at(index))));
        }
    }
}

void PlayQueuePeekTest::lookingChangesNothing_data()
{
    itIsWhatNextGives_data();
}

// A queue that is looked at before and after every call, and a twin that never is, stay the same.
void PlayQueuePeekTest::lookingChangesNothing()
{
    QFETCH(quint32, run);
    Dice dice { run * 40503u + 7 };
    Dice twinDice = dice;
    PlayQueue q;
    PlayQueue twin;
    QSignalSpy entries(&q, &PlayQueue::entriesChanged);
    QSignalSpy current(&q, &PlayQueue::currentChanged);
    QSignalSpy mode(&q, &PlayQueue::modeChanged);
    QSignalSpy twinEntries(&twin, &PlayQueue::entriesChanged);
    QSignalSpy twinCurrent(&twin, &PlayQueue::currentChanged);
    QSignalSpy twinMode(&twin, &PlayQueue::modeChanged);
    for (int step = 0; step < 80; ++step) {
        QSignalSpy note(&q, &PlayQueue::nextChanged);
        (void)q.peekNext(true);
        (void)q.peekNext(false);
        (void)q.peekNext(true);
        QCOMPARE(note.size(), 0); // looking emits nothing
        const QString did = act(q, dice);
        QCOMPARE(act(twin, twinDice), did);
        QVERIFY2(state(q) == state(twin),
            qPrintable(QStringLiteral("step %1 after %2: %3 / twin %4")
                    .arg(step)
                    .arg(did, state(q), state(twin))));
    }
    // The old signals are as many as ever.
    QCOMPARE(entries.size(), twinEntries.size());
    QCOMPARE(current.size(), twinCurrent.size());
    QCOMPARE(mode.size(), twinMode.size());
}

void PlayQueuePeekTest::aChangedAnswerIsAlwaysNoted_data()
{
    itIsWhatNextGives_data();
}

// If either answer is different after a call, nextChanged was emitted during it.
void PlayQueuePeekTest::aChangedAnswerIsAlwaysNoted()
{
    QFETCH(quint32, run);
    Dice dice { run * 69069u + 1 };
    PlayQueue q;
    for (int step = 0; step < 80; ++step) {
        const Peek ended = q.peekNext(true);
        const Peek asked = q.peekNext(false);
        const QString was = state(q);
        QSignalSpy note(&q, &PlayQueue::nextChanged);
        const QString did = act(q, dice);
        if (!same(ended, q.peekNext(true)) || !same(asked, q.peekNext(false))) {
            QVERIFY2(note.size() > 0,
                qPrintable(QStringLiteral("step %1, %2: from [%3] to [%4], and nothing said so")
                        .arg(step)
                        .arg(did, was, state(q))));
        }
    }
}

QTEST_GUILESS_MAIN(PlayQueuePeekTest)
#include "tst_playqueue_peek.moc"
