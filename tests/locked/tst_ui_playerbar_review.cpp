// LOCKED ACCEPTANCE TEST (review of plan 14e). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/player-bar.md (PB-x). Runs on the
// offscreen platform, with the real player and play queue over a backend
// that only writes down what it is told. No sound is involved.
#include <QFile>
#include <QTextStream>
#include <QAction>
#include <QLabel>
#include <QSettings>
#include <QSlider>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

#include "actions/actionregistry.h"
#include "playerbar/playerbar.h"

using namespace linuxbee;
using linuxbee::ui::ActionRegistry;
using linuxbee::ui::PlayerBar;

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
    void at(qint64 ms) { emit positionChanged(ms); }
    void lasts(qint64 ms) { emit durationChanged(ms); }
    QStringList take()
    {
        const QStringList out = log;
        log.clear();
        return out;
    }
};

// The named child, or (failing the test) a stand-in, so that a missing widget
// is a failure and not a crash.
template <typename T> T *need(QWidget &w, const char16_t *name)
{
    if (T *found = w.findChild<T *>(s(name)))
        return found;
    QTest::qFail(qPrintable(QStringLiteral("no widget named ") + s(name)), __FILE__, __LINE__);
    static T standIn;
    return &standIn;
}

struct Rig {
    PlayQueue queue;
    Tape tape;
    Player player;
    ActionRegistry actions;
    PlayerBar bar;
    Rig()
        : player(&queue, &tape)
        , bar(&player, &queue, &actions)
    {
    }
    QAction *action(const char16_t *id)
    {
        if (QAction *a = actions.action(s(id)))
            return a;
        QTest::qFail("no such action", __FILE__, __LINE__);
        static QAction standIn;
        return &standIn;
    }
    void start() // three tracks, the first playing, 3:20 long
    {
        queue.playNow({ s(u"/m/Artist - One.flac"), s(u"/m/two.mp3"), s(u"/m/three.flac") }, 0);
        tape.lasts(200000);
        tape.take();
    }
};

}

class PlayerBarTest : public QObject {
    Q_OBJECT
private slots:
    void ticksFollowOutside();
    void dragHoldsHandle();
    void unknownLengthNoSeek();
    void timeEdges();
    void volumeMuteRoundTrip();
    void storedNonsenseBools();
};

void PlayerBarTest::ticksFollowOutside()
{
    // PB-STATE, PB-COMMANDS, PB-VOLUME: changes made outside the bar show on it.
    Rig r;
    r.start();
    auto *play = need<QToolButton>(r.bar, u"playPauseButton");
    QCOMPARE(play->property("playing").toBool(), true);
    r.player.playPause();
    QCOMPARE(play->property("playing").toBool(), false);
    r.player.setMuted(true);
    QVERIFY(r.action(u"controls.mute")->isChecked());
    r.player.setStopAfterCurrent(true);
    QVERIFY(r.action(u"controls.stopAfterCurrent")->isChecked());
    r.queue.setRepeat(PlayQueue::Repeat::One);
    QVERIFY(r.action(u"controls.repeat")->isChecked());
    r.queue.setShuffle(true, 3);
    QVERIFY(r.action(u"controls.shuffle")->isChecked());
    r.player.setVolume(0.37);
    QCOMPARE(need<QSlider>(r.bar, u"volumeSlider")->value(), 37);
    QCOMPARE(r.player.volume(), 0.37); // no echo through the slider
}

void PlayerBarTest::dragHoldsHandle()
{
    // PB-SEEK: while the handle is held, reports do not move it and nothing is sent.
    Rig r;
    r.start();
    auto *seek = need<QSlider>(r.bar, u"seekSlider");
    r.tape.at(10000);
    seek->setSliderDown(true);
    seek->setSliderPosition(120000);
    r.tape.at(11000);
    r.tape.at(12000);
    QCOMPARE(seek->sliderPosition(), 120000);
    QVERIFY(r.tape.log.isEmpty());
    seek->setSliderDown(false);
    QCOMPARE(r.tape.take(), QStringList({ s(u"seek 120000") }));
}

void PlayerBarTest::unknownLengthNoSeek()
{
    // PB-STATE: no seeking while the length is unknown or 0.
    Rig r;
    auto *seek = need<QSlider>(r.bar, u"seekSlider");
    r.queue.playNow({ s(u"/m/a.flac"), s(u"/m/b.flac") }, 0);
    QVERIFY(!seek->isEnabled());
    r.tape.lasts(0);
    QVERIFY(!seek->isEnabled());
    r.tape.lasts(5000);
    QVERIFY(seek->isEnabled());
    r.player.next();
    QVERIFY(!seek->isEnabled());
}

void PlayerBarTest::timeEdges()
{
    // PB-TIME
    QCOMPARE(PlayerBar::formatTime(0), s(u"0:00"));
    QCOMPARE(PlayerBar::formatTime(59999), s(u"0:59"));
    QCOMPARE(PlayerBar::formatTime(3599999), s(u"59:59"));
    QCOMPARE(PlayerBar::formatTime(3600000), s(u"1:00:00"));
    QCOMPARE(PlayerBar::formatTime(36061000), s(u"10:01:01"));
    QCOMPARE(PlayerBar::formatTime(-1), s(u"0:00"));
    QCOMPARE(PlayerBar::formatTime(-3600000), s(u"0:00"));
}

void PlayerBarTest::volumeMuteRoundTrip()
{
    // PB-VOLUME: muting does not move the slider; unmuting keeps the volume.
    Rig r;
    auto *slider = need<QSlider>(r.bar, u"volumeSlider");
    slider->setValue(40);
    r.action(u"controls.mute")->trigger();
    QVERIFY(r.player.isMuted());
    QCOMPARE(slider->value(), 40);
    r.action(u"controls.mute")->trigger();
    QVERIFY(!r.player.isMuted());
    QCOMPARE(r.player.volume(), 0.4);
}

void PlayerBarTest::storedNonsenseBools()
{
    // PB-STORE: a senseless value gives not muted and no shuffle. Planted as the
    // text a later run reads.
    QTemporaryDir dir;
    const QString path = dir.path() + s(u"/odd.ini");
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QTextStream(&f) << "[player]\nvolume=abc\nmuted=banana\nrepeat=all\nshuffle=banana\n";
    }
    QSettings odd(path, QSettings::IniFormat);
    Rig r;
    r.player.setVolume(0.2);
    r.player.setMuted(true);
    r.queue.setShuffle(true, 1);
    r.bar.restoreState(odd);
    QCOMPARE(r.player.volume(), 1.0);
    QVERIFY(!r.player.isMuted());
    QVERIFY(!r.action(u"controls.mute")->isChecked());
    QVERIFY(r.queue.repeat() == PlayQueue::Repeat::All);
    QVERIFY(!r.queue.shuffle());
    QVERIFY(!r.action(u"controls.shuffle")->isChecked());
    // and the honest text still reads back
    const QString path2 = dir.path() + s(u"/fine.ini");
    {
        QFile f(path2);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QTextStream(&f) << "[player]\nvolume=60\nmuted=true\nshuffle=true\n";
    }
    QSettings fine(path2, QSettings::IniFormat);
    Rig r2;
    r2.bar.restoreState(fine);
    QCOMPARE(r2.player.volume(), 0.6);
    QVERIFY(r2.player.isMuted());
    QVERIFY(r2.queue.shuffle());
}

QTEST_MAIN(PlayerBarTest)
#include "tst_ui_playerbar_review.moc"
