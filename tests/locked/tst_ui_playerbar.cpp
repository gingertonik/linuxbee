// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/player-bar.md (PB-x). Runs on the
// offscreen platform, with the real player and play queue over a backend
// that only writes down what it is told. No sound is involved.
#include <QFile>
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

// A settings file as the next run of the program reads it. Within one run Qt
// hands back the value that was put in (a bool stays a bool); read from disk
// by a later run, every value is text. Copying the file gives the later view.
QString nextRun(QSettings &settings)
{
    static int count = 0;
    settings.sync();
    const QString copy = settings.fileName() + QStringLiteral(".run%1.ini").arg(++count);
    QFile::remove(copy);
    QFile::copy(settings.fileName(), copy);
    return copy;
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
    void time_data();
    void time();
    void structure();
    void transport();
    void buttonsAreTheCommands();
    void stateShown();
    void seekBarFollows();
    void seekBarDragged();
    void seekBarStepped();
    void title();
    void volume();
    void muteAndStopAfter();
    void repeatAndShuffle();
    void stored();
    void gone();
};

void PlayerBarTest::time_data()
{
    QTest::addColumn<qint64>("ms");
    QTest::addColumn<QString>("text");
    // PB-TIME
    QTest::newRow("zero") << qint64(0) << s(u"0:00");
    QTest::newRow("under a second") << qint64(999) << s(u"0:00");
    QTest::newRow("a second") << qint64(1000) << s(u"0:01");
    QTest::newRow("59.9") << qint64(59999) << s(u"0:59");
    QTest::newRow("a minute") << qint64(60000) << s(u"1:00");
    QTest::newRow("3:20") << qint64(200000) << s(u"3:20");
    QTest::newRow("59:59") << qint64(3599000) << s(u"59:59");
    QTest::newRow("an hour") << qint64(3600000) << s(u"1:00:00");
    QTest::newRow("long") << qint64(45296000) << s(u"12:34:56");
    QTest::newRow("negative") << qint64(-5000) << s(u"0:00");
}

void PlayerBarTest::time()
{
    QFETCH(qint64, ms);
    QFETCH(QString, text);
    QCOMPARE(PlayerBar::formatTime(ms), text);
}

void PlayerBarTest::structure()
{
    // PB-FRAME
    Rig r;
    for (const char16_t *n : { u"previousButton", u"playPauseButton", u"stopButton", u"nextButton", u"muteButton" })
        QVERIFY2(r.bar.findChild<QToolButton *>(s(n)), qPrintable(s(n)));
    for (const char16_t *n : { u"seekSlider", u"volumeSlider" })
        QVERIFY2(r.bar.findChild<QSlider *>(s(n)), qPrintable(s(n)));
    for (const char16_t *n : { u"positionLabel", u"durationLabel", u"titleLabel" })
        QVERIFY2(r.bar.findChild<QLabel *>(s(n)), qPrintable(s(n)));
    auto *seek = need<QSlider>(r.bar, u"seekSlider");
    auto *vol = need<QSlider>(r.bar, u"volumeSlider");
    QVERIFY(seek->orientation() == Qt::Horizontal);
    QVERIFY(vol->orientation() == Qt::Horizontal);
    QCOMPARE(vol->minimum(), 0);
    QCOMPARE(vol->maximum(), 100);
    // With nothing playing.
    QCOMPARE(vol->value(), 100);
    QVERIFY(!seek->isEnabled());
    QCOMPARE(seek->value(), 0);
    QCOMPARE(need<QLabel>(r.bar, u"positionLabel")->text(), s(u"0:00"));
    QCOMPARE(need<QLabel>(r.bar, u"durationLabel")->text(), s(u"0:00"));
    QCOMPARE(need<QLabel>(r.bar, u"titleLabel")->text(), QString());
    r.bar.resize(900, 72);
    r.bar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&r.bar));
    // Left to right: previous, play, stop, next; the seek bar between the two times; volume at the right.
    auto x = [&r](const char16_t *n) { return need<QWidget>(r.bar, n)->mapTo(&r.bar, QPoint(0, 0)).x(); };
    QVERIFY(x(u"previousButton") < x(u"playPauseButton"));
    QVERIFY(x(u"playPauseButton") < x(u"stopButton"));
    QVERIFY(x(u"stopButton") < x(u"nextButton"));
    QVERIFY(x(u"positionLabel") < x(u"seekSlider"));
    QVERIFY(x(u"seekSlider") < x(u"durationLabel"));
    QVERIFY(x(u"muteButton") < x(u"volumeSlider"));
    QVERIFY(x(u"volumeSlider") > 600);
    QVERIFY(seek->width() > 400); // the seek bar takes the room
}

void PlayerBarTest::transport()
{
    // PB-COMMANDS
    Rig r;
    for (const char16_t *id : { u"controls.playPause", u"controls.stop", u"controls.next", u"controls.previous",
             u"controls.stopAfterCurrent", u"controls.mute", u"controls.volumeUp", u"controls.volumeDown", u"controls.repeat",
             u"controls.shuffle" }) {
        QVERIFY2(r.actions.isImplemented(s(id)), qPrintable(s(id)));
        QVERIFY2(r.action(id)->isEnabled(), qPrintable(s(id)));
    }
    r.queue.queueLast({ s(u"/m/a.flac"), s(u"/m/b.flac"), s(u"/m/c.flac") });
    r.action(u"controls.playPause")->trigger();
    QCOMPARE(r.tape.take(), QStringList({ s(u"load /m/a.flac"), s(u"play") }));
    r.action(u"controls.playPause")->trigger();
    QCOMPARE(r.tape.take(), QStringList({ s(u"pause") }));
    QVERIFY(r.player.state() == Player::State::Paused);
    r.action(u"controls.next")->trigger();
    QCOMPARE(r.player.currentPath(), s(u"/m/b.flac"));
    r.action(u"controls.next")->trigger();
    r.action(u"controls.previous")->trigger();
    QCOMPARE(r.player.currentPath(), s(u"/m/b.flac"));
    r.tape.take();
    r.action(u"controls.stop")->trigger();
    QCOMPARE(r.tape.take(), QStringList({ s(u"stop") }));
    QVERIFY(r.player.state() == Player::State::Stopped);
}

void PlayerBarTest::buttonsAreTheCommands()
{
    // PB-FRAME: a button is its command: same behaviour, same enabled state, same tick.
    Rig r;
    r.bar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&r.bar));
    QCOMPARE(need<QToolButton>(r.bar, u"previousButton")->defaultAction(), r.action(u"controls.previous"));
    QCOMPARE(need<QToolButton>(r.bar, u"playPauseButton")->defaultAction(), r.action(u"controls.playPause"));
    QCOMPARE(need<QToolButton>(r.bar, u"stopButton")->defaultAction(), r.action(u"controls.stop"));
    QCOMPARE(need<QToolButton>(r.bar, u"nextButton")->defaultAction(), r.action(u"controls.next"));
    QCOMPARE(need<QToolButton>(r.bar, u"muteButton")->defaultAction(), r.action(u"controls.mute"));
    r.queue.queueLast({ s(u"/m/a.flac"), s(u"/m/b.flac") });
    QTest::mouseClick(need<QToolButton>(r.bar, u"playPauseButton"), Qt::LeftButton);
    QVERIFY(r.player.state() == Player::State::Playing);
    QTest::mouseClick(need<QToolButton>(r.bar, u"nextButton"), Qt::LeftButton);
    QCOMPARE(r.player.currentPath(), s(u"/m/b.flac"));
    QTest::mouseClick(need<QToolButton>(r.bar, u"muteButton"), Qt::LeftButton);
    QVERIFY(r.player.isMuted());
    QVERIFY(need<QToolButton>(r.bar, u"muteButton")->isChecked());
    QTest::mouseClick(need<QToolButton>(r.bar, u"stopButton"), Qt::LeftButton);
    QVERIFY(r.player.state() == Player::State::Stopped);
}

void PlayerBarTest::stateShown()
{
    // PB-STATE
    Rig r;
    auto *play = need<QToolButton>(r.bar, u"playPauseButton");
    auto *seek = need<QSlider>(r.bar, u"seekSlider");
    QCOMPARE(play->property("playing").toBool(), false);
    r.queue.playNow({ s(u"/m/a.flac"), s(u"/m/b.flac") }, 0);
    QCOMPARE(play->property("playing").toBool(), true);
    QVERIFY(!seek->isEnabled()); // the length is not known yet
    QCOMPARE(need<QLabel>(r.bar, u"durationLabel")->text(), s(u"0:00"));
    r.tape.lasts(200000);
    QVERIFY(seek->isEnabled());
    QCOMPARE(seek->maximum(), 200000);
    QCOMPARE(need<QLabel>(r.bar, u"durationLabel")->text(), s(u"3:20"));
    r.player.pause();
    QCOMPARE(play->property("playing").toBool(), false);
    QVERIFY(seek->isEnabled()); // paused: still seekable
    r.tape.at(42000);
    r.player.stop();
    QCOMPARE(play->property("playing").toBool(), false);
    QVERIFY(!seek->isEnabled());
    QCOMPARE(seek->value(), 0);
    QCOMPARE(need<QLabel>(r.bar, u"positionLabel")->text(), s(u"0:00"));
    // The next track starts with its length unknown again.
    r.player.play();
    r.tape.lasts(200000);
    r.player.next();
    QVERIFY(!seek->isEnabled());
    QCOMPARE(need<QLabel>(r.bar, u"durationLabel")->text(), s(u"0:00"));
}

void PlayerBarTest::seekBarFollows()
{
    // PB-SEEK
    Rig r;
    r.start();
    auto *seek = need<QSlider>(r.bar, u"seekSlider");
    r.tape.at(42000);
    QCOMPARE(seek->value(), 42000);
    QCOMPARE(need<QLabel>(r.bar, u"positionLabel")->text(), s(u"0:42"));
    r.tape.at(125999);
    QCOMPARE(seek->value(), 125999);
    QCOMPARE(need<QLabel>(r.bar, u"positionLabel")->text(), s(u"2:05"));
    QVERIFY(r.tape.log.isEmpty()); // following the music is not seeking
}

void PlayerBarTest::seekBarDragged()
{
    // PB-SEEK: while the handle is held, the music does not pull it away; letting go seeks once.
    Rig r;
    r.start();
    auto *seek = need<QSlider>(r.bar, u"seekSlider");
    r.tape.at(10000);
    seek->setSliderDown(true);
    seek->setSliderPosition(150000);
    r.tape.at(11000);
    r.tape.at(12000);
    QCOMPARE(seek->sliderPosition(), 150000);
    QVERIFY(r.tape.log.isEmpty()); // nothing yet
    seek->setSliderPosition(90000);
    QVERIFY(r.tape.log.isEmpty());
    seek->setSliderDown(false);
    QCOMPARE(r.tape.take(), QStringList({ s(u"seek 90000") }));
    QCOMPARE(r.player.positionMs(), 90000);
    QCOMPARE(seek->value(), 90000);
    QCOMPARE(need<QLabel>(r.bar, u"positionLabel")->text(), s(u"1:30"));
    r.tape.at(91000); // and it follows again
    QCOMPARE(seek->value(), 91000);
    QVERIFY(r.tape.log.isEmpty());
}

void PlayerBarTest::seekBarStepped()
{
    // PB-SEEK: a step (a click beside the handle, a key) seeks at once.
    Rig r;
    r.start();
    r.bar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&r.bar));
    auto *seek = need<QSlider>(r.bar, u"seekSlider");
    r.tape.at(50000);
    seek->setFocus();
    seek->triggerAction(QAbstractSlider::SliderPageStepAdd);
    const QStringList log = r.tape.take();
    QCOMPARE(log.size(), 1);
    QVERIFY2(log.first().startsWith(s(u"seek ")), qPrintable(log.first()));
    const qint64 target = log.first().mid(5).toLongLong();
    QVERIFY2(target > 50000 && target <= 200000, qPrintable(log.first()));
    QCOMPARE(r.player.positionMs(), target);
    seek->triggerAction(QAbstractSlider::SliderToMinimum);
    QCOMPARE(r.tape.take(), QStringList({ s(u"seek 0") }));
}

void PlayerBarTest::title()
{
    // PB-TITLE
    Rig r;
    auto *label = need<QLabel>(r.bar, u"titleLabel");
    r.start();
    QCOMPARE(label->text(), s(u"Artist - One")); // the file's name without its extension
    r.bar.setTrackText(s(u"Somebody \u2013 Something"));
    QCOMPARE(label->text(), s(u"Somebody \u2013 Something"));
    r.tape.at(5000);
    r.player.pause();
    r.player.play();
    QCOMPARE(label->text(), s(u"Somebody \u2013 Something")); // it lasts while the track does
    r.player.next();
    QCOMPARE(label->text(), s(u"two"));
    r.player.stop();
    QCOMPARE(label->text(), s(u"two")); // stopping keeps the track on show
    // A bar made while something plays shows it straight away.
    ActionRegistry other;
    PlayerBar second(&r.player, &r.queue, &other);
    QCOMPARE(need<QLabel>(second, u"titleLabel")->text(), s(u"two"));
}

void PlayerBarTest::volume()
{
    // PB-VOLUME
    Rig r;
    auto *slider = need<QSlider>(r.bar, u"volumeSlider");
    slider->setValue(40);
    QCOMPARE(r.player.volume(), 0.4);
    QCOMPARE(r.tape.volume, 0.4);
    r.player.setVolume(0.75); // from elsewhere
    QCOMPARE(slider->value(), 75);
    QCOMPARE(r.player.volume(), 0.75); // and no echo back
    r.action(u"controls.volumeUp")->trigger();
    QCOMPARE(slider->value(), 80);
    QCOMPARE(r.player.volume(), 0.8);
    r.action(u"controls.volumeDown")->trigger();
    r.action(u"controls.volumeDown")->trigger();
    QCOMPARE(slider->value(), 70);
    slider->setValue(98);
    r.action(u"controls.volumeUp")->trigger();
    QCOMPARE(slider->value(), 100);
    r.action(u"controls.volumeUp")->trigger();
    QCOMPARE(r.player.volume(), 1.0);
    slider->setValue(3);
    r.action(u"controls.volumeDown")->trigger();
    QCOMPARE(slider->value(), 0);
    QCOMPARE(r.player.volume(), 0.0);
}

void PlayerBarTest::muteAndStopAfter()
{
    // PB-COMMANDS: the two tick-box commands and the player stay in step, whoever changes them.
    Rig r;
    QAction *mute = r.action(u"controls.mute");
    QAction *stopAfter = r.action(u"controls.stopAfterCurrent");
    QVERIFY(!mute->isChecked() && !stopAfter->isChecked());
    mute->trigger();
    QVERIFY(r.player.isMuted());
    QCOMPARE(r.tape.volume, 0.0);
    QCOMPARE(need<QSlider>(r.bar, u"volumeSlider")->value(), 100); // muting does not move the slider
    r.player.setMuted(false);
    QVERIFY(!mute->isChecked());
    stopAfter->trigger();
    QVERIFY(r.player.stopAfterCurrent());
    // The player clears it by itself when it has been used.
    r.queue.playNow({ s(u"/m/a.flac"), s(u"/m/b.flac") }, 0);
    emit r.tape.ended();
    QVERIFY(!r.player.stopAfterCurrent());
    QVERIFY(!stopAfter->isChecked());
    // A bar made when the player is already muted starts with the tick.
    r.player.setMuted(true);
    ActionRegistry other;
    PlayerBar second(&r.player, &r.queue, &other);
    QVERIFY(other.action(s(u"controls.mute"))->isChecked());
}

void PlayerBarTest::repeatAndShuffle()
{
    // PB-COMMANDS
    Rig r;
    QAction *repeat = r.action(u"controls.repeat");
    QAction *shuffle = r.action(u"controls.shuffle");
    QVERIFY(!repeat->isChecked() && !shuffle->isChecked());
    repeat->trigger();
    QVERIFY(r.queue.repeat() == PlayQueue::Repeat::All);
    repeat->trigger();
    QVERIFY(r.queue.repeat() == PlayQueue::Repeat::Off);
    r.queue.setRepeat(PlayQueue::Repeat::One); // set elsewhere: the tick means "some repeat is on"
    QVERIFY(repeat->isChecked());
    repeat->trigger();
    QVERIFY(r.queue.repeat() == PlayQueue::Repeat::Off);
    shuffle->trigger();
    QVERIFY(r.queue.shuffle());
    r.queue.setShuffle(false, 1);
    QVERIFY(!shuffle->isChecked());
}

void PlayerBarTest::stored()
{
    // PB-STORE
    QTemporaryDir dir;
    QSettings settings(dir.path() + s(u"/p.ini"), QSettings::IniFormat);
    {
        Rig r;
        r.player.setVolume(0.35);
        r.player.setMuted(true);
        r.queue.setRepeat(PlayQueue::Repeat::One);
        r.queue.setShuffle(true, 5);
        r.bar.saveState(settings);
    }
    settings.sync();
    QCOMPARE(settings.value(s(u"player/volume")).toInt(), 35);
    QCOMPARE(settings.value(s(u"player/muted")).toBool(), true);
    QCOMPARE(settings.value(s(u"player/repeat")).toString(), s(u"one"));
    QCOMPARE(settings.value(s(u"player/shuffle")).toBool(), true);
    {
        QSettings later(nextRun(settings), QSettings::IniFormat); // as the next run reads it
        Rig r;
        r.bar.restoreState(later);
        QCOMPARE(r.player.volume(), 0.35);
        QCOMPARE(need<QSlider>(r.bar, u"volumeSlider")->value(), 35);
        QVERIFY(r.player.isMuted());
        QVERIFY(r.action(u"controls.mute")->isChecked());
        QVERIFY(r.queue.repeat() == PlayQueue::Repeat::One);
        QVERIFY(r.action(u"controls.repeat")->isChecked());
        QVERIFY(r.queue.shuffle());
        QVERIFY(r.action(u"controls.shuffle")->isChecked());
    }
    // Nothing stored, or nonsense: full volume, not muted, no repeat, no shuffle.
    QSettings empty(dir.path() + s(u"/empty.ini"), QSettings::IniFormat);
    empty.setValue(s(u"player/volume"), 250);
    empty.setValue(s(u"player/repeat"), s(u"forever"));
    QSettings strange(nextRun(empty), QSettings::IniFormat);
    Rig r;
    r.player.setVolume(0.2);
    r.queue.setRepeat(PlayQueue::Repeat::All);
    r.bar.restoreState(strange);
    QCOMPARE(r.player.volume(), 1.0);
    QVERIFY(!r.player.isMuted());
    QVERIFY(r.queue.repeat() == PlayQueue::Repeat::Off);
    QVERIFY(!r.queue.shuffle());
}

void PlayerBarTest::gone()
{
    // When the bar is destroyed its commands are free again, and the player lives on.
    PlayQueue queue;
    Tape tape;
    Player player(&queue, &tape);
    ActionRegistry actions;
    {
        PlayerBar bar(&player, &queue, &actions);
        QVERIFY(actions.isImplemented(s(u"controls.playPause")));
    }
    QVERIFY(!actions.isImplemented(s(u"controls.playPause")));
    QVERIFY(!actions.action(s(u"controls.next"))->isEnabled());
    queue.playNow({ s(u"/m/a.flac") }, 0); // nothing dangling listens to these
    tape.at(1000);
    player.setVolume(0.5);
    player.setMuted(true);
    queue.setRepeat(PlayQueue::Repeat::All);
    QVERIFY(player.state() == Player::State::Playing);
}

QTEST_MAIN(PlayerBarTest)
#include "tst_ui_playerbar.moc"
