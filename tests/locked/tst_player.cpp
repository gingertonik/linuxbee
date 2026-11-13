// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/player.md (X-x). The backend here
// is a stand-in that writes down what it is told and reports what the test
// makes it report; no sound is involved and no clock.
#include <QSignalSpy>
#include <QtTest>

#include "playback/player.h"

using namespace linuxbee;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

class Tape : public AudioBackend {
    Q_OBJECT
public:
    QStringList log;
    double volume = -1;
    void load(const QString &path) override { log.append(QStringLiteral("load ") + path); }
    void play() override { log.append(QStringLiteral("play")); }
    void pause() override { log.append(QStringLiteral("pause")); }
    void stop() override { log.append(QStringLiteral("stop")); }
    void seek(qint64 ms) override { log.append(QStringLiteral("seek %1").arg(ms)); }
    void setVolume(double linear) override { volume = linear; }

    // What a real backend would report.
    void at(qint64 ms) { emit positionChanged(ms); }
    void lasts(qint64 ms) { emit durationChanged(ms); }
    void end() { emit ended(); }
    void fail(const char16_t *why) { emit failed(QString::fromUtf16(why)); }
    // Plays from `from` to `to` in steps of a second, as a backend reports it.
    void run(qint64 from, qint64 to)
    {
        for (qint64 ms = from; ms <= to; ms += 1000)
            emit positionChanged(ms);
    }
    QStringList take()
    {
        const QStringList out = log;
        log.clear();
        return out;
    }
};

struct Rig {
    PlayQueue queue;
    Tape tape;
    Player player;
    QSignalSpy state;
    QSignalSpy track;
    QSignalSpy played;
    QSignalSpy skipped;
    QSignalSpy errors;
    Rig()
        : player(&queue, &tape)
        , state(&player, &Player::stateChanged)
        , track(&player, &Player::trackChanged)
        , played(&player, &Player::playCounted)
        , skipped(&player, &Player::skipCounted)
        , errors(&player, &Player::playbackError)
    {
    }
    QStringList tracks()
    {
        QStringList out;
        for (const auto &e : track)
            out.append(e.at(0).toString());
        track.clear();
        return out;
    }
    QStringList counted(QSignalSpy &spy)
    {
        QStringList out;
        for (const auto &e : spy)
            out.append(e.at(0).toString());
        spy.clear();
        return out;
    }
};

const QStringList kThree = { s(u"/m/a.flac"), s(u"/m/b.flac"), s(u"/m/c.flac") };
const qint64 kLong = 200000; // 3:20

}

class PlayerTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qRegisterMetaType<linuxbee::Player::State>(); }
    void initial();
    void playNowStarts();
    void playPauseStop();
    void playFromStopped();
    void endOfTrack();
    void endOfQueue();
    void repeatOne();
    void nextAndPrevious();
    void previousRestarts();
    void queueEdits();
    void seeking();
    void positionAndDuration();
    void countPlayed();
    void countSkipped();
    void countNeither();
    void countUsesTimeHeard();
    void stopAfterCurrent();
    void volume();
    void trackVolume();
    void errors();
    void errorsEverywhere();
};

void PlayerTest::initial()
{
    Rig r;
    QVERIFY(r.player.state() == Player::State::Stopped);
    QCOMPARE(r.player.currentPath(), QString());
    QCOMPARE(r.player.positionMs(), 0);
    QCOMPARE(r.player.durationMs(), 0);
    QCOMPARE(r.player.volume(), 1.0);
    QVERIFY(!r.player.isMuted());
    QVERIFY(!r.player.stopAfterCurrent());
    // With an empty queue nothing does anything.
    r.player.play();
    r.player.playPause();
    r.player.next();
    r.player.previous();
    r.player.pause();
    r.player.stop();
    r.player.seek(5000);
    QVERIFY(r.player.state() == Player::State::Stopped);
    QVERIFY(r.tape.log.isEmpty());
    QCOMPARE(r.state.size(), 0);
    QCOMPARE(r.track.size(), 0);
}

void PlayerTest::playNowStarts()
{
    // X-QUEUE: a new current entry in the queue is played.
    Rig r;
    r.queue.playNow(kThree, 1);
    QCOMPARE(r.tape.take(), QStringList({ s(u"load /m/b.flac"), s(u"play") }));
    QVERIFY(r.player.state() == Player::State::Playing);
    QCOMPARE(r.player.currentPath(), s(u"/m/b.flac"));
    QCOMPARE(r.tracks(), QStringList({ s(u"/m/b.flac") }));
    QCOMPARE(r.state.size(), 1);
    QVERIFY(r.state.at(0).at(0).value<Player::State>() == Player::State::Playing);
    QCOMPARE(r.tape.volume, 1.0); // the volume is set for every track
    // Choosing another entry plays it.
    r.queue.setCurrent(2);
    QCOMPARE(r.tape.take(), QStringList({ s(u"load /m/c.flac"), s(u"play") }));
    QCOMPARE(r.tracks(), QStringList({ s(u"/m/c.flac") }));
    QCOMPARE(r.state.size(), 1); // still playing: nothing to announce
    // A new list at the same index is a new track, even with the same path.
    r.queue.playNow({ s(u"/m/c.flac") }, 0);
    QCOMPARE(r.tape.take(), QStringList({ s(u"load /m/c.flac"), s(u"play") }));
    QCOMPARE(r.tracks(), QStringList({ s(u"/m/c.flac") }));
}

void PlayerTest::playPauseStop()
{
    // X-STATE
    Rig r;
    r.queue.playNow(kThree, 0);
    r.tape.take();
    r.state.clear();
    r.player.play(); // already playing
    QVERIFY(r.tape.log.isEmpty());
    r.player.pause();
    QCOMPARE(r.tape.take(), QStringList({ s(u"pause") }));
    QVERIFY(r.player.state() == Player::State::Paused);
    r.player.pause(); // already paused
    QVERIFY(r.tape.log.isEmpty());
    r.player.play(); // resumes; the file is not loaded again
    QCOMPARE(r.tape.take(), QStringList({ s(u"play") }));
    QVERIFY(r.player.state() == Player::State::Playing);
    r.player.playPause();
    QVERIFY(r.player.state() == Player::State::Paused);
    r.player.playPause();
    QVERIFY(r.player.state() == Player::State::Playing);
    QCOMPARE(r.tape.take(), QStringList({ s(u"pause"), s(u"play") }));
    QCOMPARE(r.state.size(), 4);
    r.tape.at(42000);
    r.player.stop();
    QCOMPARE(r.tape.take(), QStringList({ s(u"stop") }));
    QVERIFY(r.player.state() == Player::State::Stopped);
    QCOMPARE(r.player.positionMs(), 0);
    QCOMPARE(r.player.currentPath(), s(u"/m/a.flac")); // still the track on show
    QCOMPARE(r.queue.currentIndex(), 0); // the queue is where it was
    r.player.stop(); // already stopped
    r.player.pause();
    QVERIFY(r.tape.log.isEmpty());
    QCOMPARE(r.track.size(), 1);
    // Stop from paused.
    r.player.play();
    r.player.pause();
    r.tape.take();
    r.player.stop();
    QCOMPARE(r.tape.take(), QStringList({ s(u"stop") }));
}

void PlayerTest::playFromStopped()
{
    // X-STATE: Play when stopped starts the queue's current entry from the beginning.
    Rig r;
    r.queue.playNow(kThree, 1);
    r.tape.at(30000);
    r.player.stop();
    r.tape.take();
    r.tracks();
    r.player.play();
    QCOMPARE(r.tape.take(), QStringList({ s(u"load /m/b.flac"), s(u"play") }));
    QCOMPARE(r.player.positionMs(), 0);
    QVERIFY(r.tracks().isEmpty()); // the same track: nothing to announce
    // With nothing current, Play starts where Next would.
    Rig fresh;
    fresh.queue.queueLast(kThree);
    QCOMPARE(fresh.queue.currentIndex(), -1);
    QVERIFY(fresh.tape.log.isEmpty()); // queueing alone plays nothing
    fresh.player.play();
    QCOMPARE(fresh.tape.take(), QStringList({ s(u"load /m/a.flac"), s(u"play") }));
    QCOMPARE(fresh.queue.currentIndex(), 0);
    QCOMPARE(fresh.tracks(), QStringList({ s(u"/m/a.flac") }));
    // playPause does the same.
    Rig other;
    other.queue.queueLast(kThree);
    other.player.playPause();
    QVERIFY(other.player.state() == Player::State::Playing);
}

void PlayerTest::endOfTrack()
{
    // X-END
    Rig r;
    r.queue.playNow(kThree, 0);
    r.tape.take();
    r.tracks();
    r.tape.lasts(kLong);
    r.tape.run(0, kLong);
    r.tape.end();
    QCOMPARE(r.queue.currentIndex(), 1);
    QCOMPARE(r.tape.take(), QStringList({ s(u"load /m/b.flac"), s(u"play") }));
    QCOMPARE(r.tracks(), QStringList({ s(u"/m/b.flac") }));
    QVERIFY(r.player.state() == Player::State::Playing);
    QCOMPARE(r.player.positionMs(), 0);
    QCOMPARE(r.player.durationMs(), 0); // not known yet for the new track
    QCOMPARE(r.state.size(), 1); // playing throughout
}

void PlayerTest::endOfQueue()
{
    Rig r;
    r.queue.playNow(kThree, 2);
    r.tape.take();
    r.tape.lasts(kLong);
    r.tape.run(0, kLong);
    r.tape.end();
    QCOMPARE(r.tape.take(), QStringList({ s(u"stop") }));
    QVERIFY(r.player.state() == Player::State::Stopped);
    QCOMPARE(r.player.positionMs(), 0);
    QCOMPARE(r.queue.currentIndex(), 2);
    QCOMPARE(r.player.currentPath(), s(u"/m/c.flac"));
    // Play starts the last track again.
    r.player.play();
    QCOMPARE(r.tape.take(), QStringList({ s(u"load /m/c.flac"), s(u"play") }));
    // With repeat all, the end of the last track goes to the first.
    r.queue.setRepeat(PlayQueue::Repeat::All);
    r.tape.end();
    QCOMPARE(r.tape.take(), QStringList({ s(u"load /m/a.flac"), s(u"play") }));
    QCOMPARE(r.queue.currentIndex(), 0);
}

void PlayerTest::repeatOne()
{
    Rig r;
    r.queue.playNow(kThree, 1);
    r.queue.setRepeat(PlayQueue::Repeat::One);
    r.tape.take();
    r.tracks();
    r.tape.lasts(kLong);
    r.tape.run(0, kLong);
    r.tape.end();
    // The same entry again, from the top: loaded afresh, and the play is counted each time round.
    QCOMPARE(r.tape.take(), QStringList({ s(u"load /m/b.flac"), s(u"play") }));
    QCOMPARE(r.counted(r.played), QStringList({ s(u"/m/b.flac") }));
    QVERIFY(r.tracks().isEmpty());
    QCOMPARE(r.player.positionMs(), 0);
    r.tape.lasts(kLong);
    r.tape.run(0, kLong);
    r.tape.end();
    QCOMPARE(r.counted(r.played), QStringList({ s(u"/m/b.flac") }));
    // The user's own Next still moves on.
    r.player.next();
    QCOMPARE(r.player.currentPath(), s(u"/m/c.flac"));
}

void PlayerTest::nextAndPrevious()
{
    // X-NEXT
    Rig r;
    r.queue.playNow(kThree, 0);
    r.tape.take();
    r.player.next();
    QCOMPARE(r.tape.take(), QStringList({ s(u"load /m/b.flac"), s(u"play") }));
    QCOMPARE(r.queue.currentIndex(), 1);
    r.player.next();
    QCOMPARE(r.player.currentPath(), s(u"/m/c.flac"));
    r.tape.take();
    r.player.next(); // nowhere to go: the track plays on
    QVERIFY(r.tape.log.isEmpty());
    QVERIFY(r.player.state() == Player::State::Playing);
    QCOMPARE(r.player.currentPath(), s(u"/m/c.flac"));
    r.player.previous();
    QCOMPARE(r.tape.take(), QStringList({ s(u"load /m/b.flac"), s(u"play") }));
    // Next and Previous start playing whatever the state was.
    r.player.pause();
    r.player.next();
    QVERIFY(r.player.state() == Player::State::Playing);
    QCOMPARE(r.player.currentPath(), s(u"/m/c.flac"));
    r.player.stop();
    r.tape.take();
    r.player.previous();
    QCOMPARE(r.tape.take(), QStringList({ s(u"load /m/b.flac"), s(u"play") }));
    QVERIFY(r.player.state() == Player::State::Playing);
}

void PlayerTest::previousRestarts()
{
    // X-NEXT: more than three seconds in, Previous goes back to the start of the track.
    Rig r;
    r.queue.playNow(kThree, 1);
    r.tape.take();
    r.tape.at(3000);
    r.player.previous(); // exactly three seconds: the track before
    QCOMPARE(r.tape.take(), QStringList({ s(u"load /m/a.flac"), s(u"play") }));
    r.player.next();
    r.tape.take();
    r.tape.at(3001);
    r.player.previous();
    QCOMPARE(r.tape.take(), QStringList({ s(u"seek 0") }));
    QCOMPARE(r.player.currentPath(), s(u"/m/b.flac"));
    QCOMPARE(r.player.positionMs(), 0);
    QCOMPARE(r.queue.currentIndex(), 1);
    r.player.previous(); // now at the start: the track before
    QCOMPARE(r.tape.take(), QStringList({ s(u"load /m/a.flac"), s(u"play") }));
    // On the first track with nowhere to go back to: the start of the track.
    r.tape.at(1500);
    r.player.previous();
    QCOMPARE(r.tape.take(), QStringList({ s(u"seek 0") }));
    QCOMPARE(r.player.currentPath(), s(u"/m/a.flac"));
}

void PlayerTest::queueEdits()
{
    // X-QUEUE: only a different current entry interrupts the music.
    Rig r;
    r.queue.playNow(kThree, 1);
    r.tape.take();
    r.tracks();
    r.queue.queueNext({ s(u"/m/x.flac") });
    r.queue.queueLast({ s(u"/m/y.flac") });
    r.queue.move(0, 3); // the playing entry is now at index 0
    QCOMPARE(r.queue.currentIndex(), 0);
    r.queue.setRepeat(PlayQueue::Repeat::All);
    r.queue.setShuffle(true, 7);
    r.queue.setShuffle(false, 7);
    QVERIFY(r.tape.log.isEmpty());
    QVERIFY(r.tracks().isEmpty());
    r.queue.setCurrent(0); // the entry already playing
    QVERIFY(r.tape.log.isEmpty());
    // The playing entry is removed: it plays on, and its end goes to what followed it.
    r.queue.removeAt({ 0 });
    QCOMPARE(r.queue.currentIndex(), -1);
    QVERIFY(r.tape.log.isEmpty());
    QVERIFY(r.player.state() == Player::State::Playing);
    QCOMPARE(r.player.currentPath(), s(u"/m/b.flac"));
    r.tape.end();
    QCOMPARE(r.tape.take(), QStringList({ s(u"load /m/x.flac"), s(u"play") }));
    // The queue is emptied: the track plays on, and at its end the player stops.
    r.queue.clear();
    QVERIFY(r.tape.log.isEmpty());
    r.tape.end();
    QCOMPARE(r.tape.take(), QStringList({ s(u"stop") }));
    QVERIFY(r.player.state() == Player::State::Stopped);
}

void PlayerTest::seeking()
{
    // X-SEEK
    Rig r;
    QSignalSpy position(&r.player, &Player::positionChanged);
    r.queue.playNow(kThree, 0);
    r.tape.take();
    r.tape.lasts(kLong);
    position.clear();
    r.player.seek(60000);
    QCOMPARE(r.tape.take(), QStringList({ s(u"seek 60000") }));
    QCOMPARE(r.player.positionMs(), 60000); // at once, without waiting for the backend
    QCOMPARE(position.size(), 1);
    r.player.seek(-5);
    QCOMPARE(r.tape.take(), QStringList({ s(u"seek 0") }));
    r.player.seek(kLong + 9000); // past the end: the end
    QCOMPARE(r.tape.take(), QStringList({ s(u"seek 200000") }));
    r.player.pause();
    r.tape.take();
    r.player.seek(1000); // paused: allowed, stays paused
    QCOMPARE(r.tape.take(), QStringList({ s(u"seek 1000") }));
    QVERIFY(r.player.state() == Player::State::Paused);
    r.player.stop();
    r.tape.take();
    r.player.seek(1000); // stopped: nothing
    QVERIFY(r.tape.log.isEmpty());
    QCOMPARE(r.player.positionMs(), 0);
    // Before the duration is known there is no upper limit.
    r.player.play();
    r.tape.take();
    r.player.seek(999999);
    QCOMPARE(r.tape.take(), QStringList({ s(u"seek 999999") }));
}

void PlayerTest::positionAndDuration()
{
    Rig r;
    QSignalSpy position(&r.player, &Player::positionChanged);
    QSignalSpy duration(&r.player, &Player::durationChanged);
    // Reports with nothing loaded are ignored.
    r.tape.at(5000);
    r.tape.lasts(9000);
    r.tape.end();
    QCOMPARE(r.player.positionMs(), 0);
    QCOMPARE(r.player.durationMs(), 0);
    QVERIFY(r.tape.log.isEmpty());
    r.queue.playNow(kThree, 0);
    r.tape.lasts(kLong);
    r.tape.lasts(kLong); // the same again: no signal
    QCOMPARE(duration.size(), 1);
    QCOMPARE(r.player.durationMs(), kLong);
    r.tape.at(1000);
    r.tape.at(1000);
    r.tape.at(2000);
    QCOMPARE(position.size(), 2);
    QCOMPARE(r.player.positionMs(), 2000);
    // A new track starts at 0 with its duration unknown, and says so.
    position.clear();
    duration.clear();
    r.player.next();
    QCOMPARE(r.player.positionMs(), 0);
    QCOMPARE(r.player.durationMs(), 0);
    QCOMPARE(position.size(), 1);
    QCOMPARE(duration.size(), 1);
    // After a stop the backend's late reports are ignored.
    r.tape.at(7000);
    r.player.stop();
    r.tape.at(8000);
    QCOMPARE(r.player.positionMs(), 0);
}

void PlayerTest::countPlayed()
{
    // X-COUNT with the queue's rule: more than half, or more than 240 seconds.
    Rig r;
    r.queue.playNow(kThree, 0);
    r.tape.lasts(kLong);
    r.tape.run(0, kLong);
    QCOMPARE(r.played.size(), 0); // not before the track is over
    r.tape.end();
    QCOMPARE(r.counted(r.played), QStringList({ s(u"/m/a.flac") }));
    QCOMPARE(r.skipped.size(), 0);
    // More than half, then Next: a play.
    r.tape.lasts(kLong);
    r.tape.run(0, 101000);
    r.player.next();
    QCOMPARE(r.counted(r.played), QStringList({ s(u"/m/b.flac") }));
    // Exactly half, then Stop: not a play.
    r.tape.lasts(kLong);
    r.tape.run(0, 100000);
    r.player.stop();
    QCOMPARE(r.played.size(), 0);
    QCOMPARE(r.skipped.size(), 0);
    // The backend never reports the last stretch: an end by itself means it was heard to the end.
    r.player.play();
    r.tape.lasts(kLong);
    r.tape.run(0, 90000);
    r.tape.end();
    QCOMPARE(r.counted(r.played), QStringList({ s(u"/m/c.flac") }));
    // A changed rule is used.
    Rig strict;
    PlayCountRule rule;
    rule.playedFraction = 0.9;
    strict.player.setCountRule(rule);
    strict.queue.playNow(kThree, 0);
    strict.tape.lasts(kLong);
    strict.tape.run(0, 150000);
    strict.player.next();
    QCOMPARE(strict.played.size(), 0);
}

void PlayerTest::countSkipped()
{
    // X-COUNT: the user moved on within 20 seconds.
    Rig r;
    r.queue.playNow(kThree, 0);
    r.tape.lasts(kLong);
    r.tape.run(0, 5000);
    r.player.next();
    QCOMPARE(r.counted(r.skipped), QStringList({ s(u"/m/a.flac") }));
    r.tape.lasts(kLong);
    r.tape.run(0, 2000);
    r.player.previous(); // under three seconds in: the track before
    QCOMPARE(r.counted(r.skipped), QStringList({ s(u"/m/b.flac") }));
    r.tape.run(0, 1000);
    r.queue.setCurrent(2); // choosing another track
    QCOMPARE(r.counted(r.skipped), QStringList({ s(u"/m/a.flac") }));
    r.tape.run(0, 1000);
    r.player.stop();
    QCOMPARE(r.counted(r.skipped), QStringList({ s(u"/m/c.flac") }));
    r.player.play();
    r.queue.playNow({ s(u"/m/z.flac") }, 0); // a new list straight away
    QCOMPARE(r.counted(r.skipped), QStringList({ s(u"/m/c.flac") }));
    QCOMPARE(r.played.size(), 0);
}

void PlayerTest::countNeither()
{
    Rig r;
    r.queue.playNow(kThree, 0);
    r.tape.lasts(kLong);
    r.tape.run(0, 60000); // a minute of 3:20: neither
    r.player.next();
    QCOMPARE(r.played.size() + r.skipped.size(), 0);
    // A stopped player has nothing to count.
    r.player.stop();
    r.played.clear();
    r.skipped.clear();
    r.player.stop();
    r.player.next(); // starts c
    r.player.stop(); // c: nothing heard, ended by the user
    QCOMPARE(r.counted(r.skipped), QStringList({ s(u"/m/c.flac") }));
    // Each track is counted once at most.
    r.player.stop();
    QCOMPARE(r.played.size() + r.skipped.size(), 0);
}

void PlayerTest::countUsesTimeHeard()
{
    // X-LISTEN: what counts is time heard, not the position reached.
    Rig r;
    r.queue.playNow(kThree, 0);
    r.tape.lasts(kLong);
    r.tape.run(0, 5000);
    r.player.seek(190000); // jumping to near the end is not listening
    r.tape.run(190000, 195000);
    r.player.next();
    QCOMPARE(r.played.size(), 0);
    QCOMPARE(r.counted(r.skipped), QStringList({ s(u"/m/a.flac") })); // ten seconds heard
    // Time while paused is not heard, even if the backend's position creeps.
    r.tape.lasts(kLong);
    r.tape.run(0, 10000);
    r.player.pause();
    r.tape.at(150000);
    r.player.play();
    r.tape.run(150000, 155000);
    r.player.next();
    QCOMPARE(r.played.size(), 0);
    QCOMPARE(r.skipped.size(), 1); // fifteen seconds heard
    r.skipped.clear();
    // Hearing the first half twice is hearing more than half.
    r.tape.lasts(kLong);
    r.tape.run(0, 60000);
    r.player.seek(0);
    r.tape.run(0, 60000);
    r.player.stop();
    QCOMPARE(r.counted(r.played), QStringList({ s(u"/m/c.flac") }));
    // A position that jumps backwards by itself adds nothing.
    Rig back;
    back.queue.playNow(kThree, 0);
    back.tape.lasts(kLong);
    back.tape.run(0, 15000);
    back.tape.at(2000);
    back.tape.run(2000, 6000);
    back.player.next();
    QCOMPARE(back.skipped.size(), 1); // nineteen seconds
    QCOMPARE(back.played.size(), 0);
}

void PlayerTest::stopAfterCurrent()
{
    // X-END
    Rig r;
    QSignalSpy flag(&r.player, &Player::stopAfterCurrentChanged);
    r.queue.playNow(kThree, 0);
    r.player.setStopAfterCurrent(true);
    r.player.setStopAfterCurrent(true);
    QCOMPARE(flag.size(), 1);
    QVERIFY(r.player.stopAfterCurrent());
    r.tape.take();
    r.tracks();
    r.tape.lasts(kLong);
    r.tape.run(0, kLong);
    r.tape.end();
    QCOMPARE(r.tape.take(), QStringList({ s(u"stop") }));
    QVERIFY(r.player.state() == Player::State::Stopped);
    QCOMPARE(r.counted(r.played), QStringList({ s(u"/m/a.flac") }));
    // It applies once, and the queue has moved on so that Play continues with the next track.
    QVERIFY(!r.player.stopAfterCurrent());
    QCOMPARE(flag.size(), 2);
    QCOMPARE(r.queue.currentIndex(), 1);
    QVERIFY(r.tracks().isEmpty());
    r.player.play();
    QCOMPARE(r.tape.take(), QStringList({ s(u"load /m/b.flac"), s(u"play") }));
    QCOMPARE(r.tracks(), QStringList({ s(u"/m/b.flac") }));
    // It is about the end of a track: the user's Next does not use it up.
    r.player.setStopAfterCurrent(true);
    r.player.next();
    QVERIFY(r.player.state() == Player::State::Playing);
    QVERIFY(r.player.stopAfterCurrent());
    // On the last track it stops and the queue stays.
    r.tape.end();
    QVERIFY(r.player.state() == Player::State::Stopped);
    QCOMPARE(r.queue.currentIndex(), 2);
}

void PlayerTest::volume()
{
    // X-VOLUME
    Rig r;
    QSignalSpy vol(&r.player, &Player::volumeChanged);
    QSignalSpy mute(&r.player, &Player::mutedChanged);
    r.player.setVolume(0.5);
    QCOMPARE(r.player.volume(), 0.5);
    QCOMPARE(r.tape.volume, 0.5); // told at once, track or no track
    QCOMPARE(vol.size(), 1);
    r.player.setVolume(0.5);
    QCOMPARE(vol.size(), 1);
    r.player.setVolume(7);
    QCOMPARE(r.player.volume(), 1.0);
    r.player.setVolume(-1);
    QCOMPARE(r.player.volume(), 0.0);
    QCOMPARE(r.tape.volume, 0.0);
    r.player.setVolume(0.8);
    r.player.setMuted(true);
    r.player.setMuted(true);
    QCOMPARE(mute.size(), 1);
    QVERIFY(r.player.isMuted());
    QCOMPARE(r.tape.volume, 0.0);
    QCOMPARE(r.player.volume(), 0.8); // muting does not forget the volume
    r.player.setVolume(0.6); // changed while muted: remembered, still silent
    QCOMPARE(r.tape.volume, 0.0);
    r.tape.volume = -1;
    r.queue.playNow(kThree, 0);
    QCOMPARE(r.tape.volume, 0.0); // a new track is muted too
    r.player.setMuted(false);
    QCOMPARE(r.tape.volume, 0.6);
}

void PlayerTest::trackVolume()
{
    // X-VOLUME: a factor per track, as volume levelling gives it.
    Rig r;
    r.player.setTrackVolume([](const QString &path) {
        return path.endsWith(QLatin1String("a.flac")) ? 0.5 : (path.endsWith(QLatin1String("b.flac")) ? 2.0 : -3.0);
    });
    r.player.setVolume(0.8);
    r.queue.playNow(kThree, 0);
    QCOMPARE(r.tape.volume, 0.4);
    r.player.setVolume(0.6);
    QCOMPARE(r.tape.volume, 0.3);
    r.player.next();
    QCOMPARE(r.tape.volume, 1.0); // 0.6 x 2, held at full
    r.player.next();
    QCOMPARE(r.tape.volume, 0.0); // a factor below 0 is 0
    QCOMPARE(r.player.volume(), 0.6); // the user's volume is untouched by all this
    // The volume is right before the track starts to sound.
    Rig order;
    QStringList seen;
    order.player.setTrackVolume([&](const QString &) {
        seen = order.tape.log;
        return 0.25;
    });
    order.queue.playNow(kThree, 0);
    QVERIFY(!seen.contains(s(u"play")));
    QCOMPARE(order.tape.volume, 0.25);
}

void PlayerTest::errors()
{
    // X-ERROR: a file that cannot be played is reported, not counted, and the next one is tried.
    Rig r;
    r.queue.playNow(kThree, 0);
    r.tape.take();
    r.tape.fail(u"no such file");
    QCOMPARE(r.errors.size(), 1);
    QCOMPARE(r.errors.at(0).at(0).toString(), s(u"/m/a.flac"));
    QCOMPARE(r.errors.at(0).at(1).toString(), s(u"no such file"));
    QCOMPARE(r.tape.take(), QStringList({ s(u"load /m/b.flac"), s(u"play") }));
    QVERIFY(r.player.state() == Player::State::Playing);
    QCOMPARE(r.played.size() + r.skipped.size(), 0);
    // A failure half way through is the same.
    r.tape.lasts(kLong);
    r.tape.run(0, 150000);
    r.tape.fail(u"decoder gave up");
    QCOMPARE(r.errors.size(), 2);
    QCOMPARE(r.played.size(), 0);
    QCOMPARE(r.player.currentPath(), s(u"/m/c.flac"));
    // The last track fails: the player stops.
    r.tape.take();
    r.tape.fail(u"nope");
    QCOMPARE(r.tape.take(), QStringList({ s(u"stop") }));
    QVERIFY(r.player.state() == Player::State::Stopped);
}

void PlayerTest::errorsEverywhere()
{
    // X-ERROR: with repeat on, a queue of files that all fail must not go round for ever.
    Rig r;
    r.queue.playNow(kThree, 0);
    r.queue.setRepeat(PlayQueue::Repeat::All);
    int guard = 0;
    while (r.player.state() == Player::State::Playing && ++guard < 50)
        r.tape.fail(u"gone");
    QCOMPARE(r.errors.size(), 3); // each file was tried once
    QVERIFY(r.player.state() == Player::State::Stopped);
    // A track that plays resets the count.
    Rig mixed;
    mixed.queue.playNow(kThree, 0);
    mixed.queue.setRepeat(PlayQueue::Repeat::All);
    mixed.tape.fail(u"gone");
    mixed.tape.fail(u"gone");
    mixed.tape.end(); // c plays
    mixed.tape.fail(u"gone");
    mixed.tape.fail(u"gone");
    QVERIFY(mixed.player.state() == Player::State::Playing);
    QCOMPARE(mixed.player.currentPath(), s(u"/m/c.flac"));
    // Repeat one on a file that fails: once is enough.
    Rig one;
    one.queue.playNow({ s(u"/m/a.flac") }, 0);
    one.queue.setRepeat(PlayQueue::Repeat::One);
    one.tape.fail(u"gone");
    QCOMPARE(one.errors.size(), 1);
    QVERIFY(one.player.state() == Player::State::Stopped);
    // Play tries again.
    one.player.play();
    QVERIFY(one.player.state() == Player::State::Playing);
}

QTEST_APPLESS_MAIN(PlayerTest)
#include "tst_player.moc"
