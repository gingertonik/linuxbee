// LOCKED ACCEPTANCE TEST (repeat and shuffle on the bar). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/player-bar.md (PB-MODES). Runs on the
// offscreen platform, with the real player and play queue over a backend
// that does nothing. No sound is involved.
#include <QAction>
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

class Silent : public AudioBackend {
    Q_OBJECT
public:
    void load(const QString &) override { }
    void play() override { }
    void pause() override { }
    void stop() override { }
    void seek(qint64) override { }
    void setVolume(double) override { }
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
    Silent backend;
    Player player;
    ActionRegistry actions;
    PlayerBar bar;
    Rig()
        : player(&queue, &backend)
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
    QString mode() { return need<QToolButton>(bar, u"repeatButton")->property("mode").toString(); }
};

using R = PlayQueue::Repeat;

}

class PlayerBarModesTest : public QObject {
    Q_OBJECT
private slots:
    void structure();
    void repeatCycles();
    void repeatFollows();
    void shuffleIsTheCommand();
    void startsWithWhatIsSet();
};

void PlayerBarModesTest::structure()
{
    // PB-MODES: two more buttons, at the right end, repeat before shuffle.
    Rig r;
    auto *repeat = need<QToolButton>(r.bar, u"repeatButton");
    auto *shuffle = need<QToolButton>(r.bar, u"shuffleButton");
    QVERIFY(repeat->isCheckable());
    QVERIFY(!repeat->isChecked() && !shuffle->isChecked());
    QCOMPARE(repeat->text(), s(u"Repeat"));
    QCOMPARE(repeat->toolTip(), s(u"Repeat: off"));
    QCOMPARE(r.mode(), s(u"off"));
    QVERIFY(repeat->defaultAction() == nullptr); // it cycles; the menu's tick is on or off
    r.bar.resize(900, 72);
    r.bar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&r.bar));
    auto x = [&r](const char16_t *n) { return need<QWidget>(r.bar, n)->mapTo(&r.bar, QPoint(0, 0)).x(); };
    QVERIFY(x(u"volumeSlider") < x(u"repeatButton"));
    QVERIFY(x(u"repeatButton") < x(u"shuffleButton"));
    auto middle = [&r](const char16_t *n) {
        QWidget *w = need<QWidget>(r.bar, n);
        return w->mapTo(&r.bar, w->rect().center()).y();
    };
    QVERIFY(qAbs(middle(u"repeatButton") - middle(u"muteButton")) <= 2); // in the row of mute and volume
    QVERIFY(qAbs(middle(u"shuffleButton") - middle(u"muteButton")) <= 2);
    QVERIFY(need<QSlider>(r.bar, u"seekSlider")->width() > 400); // the seek bar keeps its room
}

void PlayerBarModesTest::repeatCycles()
{
    // PB-MODES: off, all, one, off.
    Rig r;
    auto *repeat = need<QToolButton>(r.bar, u"repeatButton");
    QAction *tick = r.action(u"controls.repeat");
    QTest::mouseClick(repeat, Qt::LeftButton);
    QVERIFY(r.queue.repeat() == R::All);
    QCOMPARE(r.mode(), s(u"all"));
    QVERIFY(repeat->isChecked());
    QCOMPARE(repeat->text(), s(u"Repeat"));
    QCOMPARE(repeat->toolTip(), s(u"Repeat: all"));
    QVERIFY(tick->isChecked());
    QTest::mouseClick(repeat, Qt::LeftButton);
    QVERIFY(r.queue.repeat() == R::One);
    QCOMPARE(r.mode(), s(u"one"));
    QVERIFY(repeat->isChecked());
    QCOMPARE(repeat->text(), s(u"Repeat 1"));
    QCOMPARE(repeat->toolTip(), s(u"Repeat: one"));
    QVERIFY(tick->isChecked());
    QTest::mouseClick(repeat, Qt::LeftButton);
    QVERIFY(r.queue.repeat() == R::Off);
    QCOMPARE(r.mode(), s(u"off"));
    QVERIFY(!repeat->isChecked());
    QCOMPARE(repeat->text(), s(u"Repeat"));
    QVERIFY(!tick->isChecked());
    QTest::mouseClick(repeat, Qt::LeftButton); // and round again
    QVERIFY(r.queue.repeat() == R::All);
}

void PlayerBarModesTest::repeatFollows()
{
    // PB-MODES: the button shows the truth, whoever changed it.
    Rig r;
    auto *repeat = need<QToolButton>(r.bar, u"repeatButton");
    r.queue.setRepeat(R::One);
    QCOMPARE(r.mode(), s(u"one"));
    QVERIFY(repeat->isChecked());
    QCOMPARE(repeat->text(), s(u"Repeat 1"));
    r.action(u"controls.repeat")->trigger(); // the menu's tick, taken off: no repeat (PB-COMMANDS)
    QVERIFY(r.queue.repeat() == R::Off);
    QCOMPARE(r.mode(), s(u"off"));
    QVERIFY(!repeat->isChecked());
    r.action(u"controls.repeat")->trigger(); // and put on: repeat all
    QCOMPARE(r.mode(), s(u"all"));
    QVERIFY(repeat->isChecked());
    QTest::mouseClick(repeat, Qt::LeftButton); // the button goes on from where the queue is
    QVERIFY(r.queue.repeat() == R::One);
    // Stored and restored as before (PB-STORE), and the button follows.
    QTemporaryDir dir;
    QSettings settings(dir.path() + s(u"/s.ini"), QSettings::IniFormat);
    r.bar.saveState(settings);
    QCOMPARE(settings.value(s(u"player/repeat")).toString(), s(u"one"));
    r.queue.setRepeat(R::Off);
    r.bar.restoreState(settings);
    QCOMPARE(r.mode(), s(u"one"));
}

void PlayerBarModesTest::shuffleIsTheCommand()
{
    // PB-MODES: the shuffle button is the command, as the other buttons are.
    Rig r;
    auto *shuffle = need<QToolButton>(r.bar, u"shuffleButton");
    QCOMPARE(shuffle->defaultAction(), r.action(u"controls.shuffle"));
    QTest::mouseClick(shuffle, Qt::LeftButton);
    QVERIFY(r.queue.shuffle());
    QVERIFY(shuffle->isChecked());
    r.queue.setShuffle(false, 1);
    QVERIFY(!shuffle->isChecked());
    QVERIFY(r.queue.repeat() == R::Off); // neither touches the other
}

void PlayerBarModesTest::startsWithWhatIsSet()
{
    // A bar created when something is already set starts right.
    PlayQueue queue;
    Silent backend;
    Player player(&queue, &backend);
    ActionRegistry actions;
    queue.setRepeat(R::One);
    queue.setShuffle(true, 5);
    PlayerBar bar(&player, &queue, &actions);
    auto *repeat = need<QToolButton>(bar, u"repeatButton");
    QCOMPARE(repeat->property("mode").toString(), s(u"one"));
    QVERIFY(repeat->isChecked());
    QCOMPARE(repeat->text(), s(u"Repeat 1"));
    QVERIFY(need<QToolButton>(bar, u"shuffleButton")->isChecked());
}

QTEST_MAIN(PlayerBarModesTest)
#include "tst_ui_playerbar_modes.moc"
