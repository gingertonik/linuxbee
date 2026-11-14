// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/playback-shell.md (PS-x). Runs on
// the offscreen platform with the real library, shells, queue, player and
// player bar, over a backend that only writes down what it is told. No
// sound is involved.
#include <QFile>
#include <QAction>
#include <QLabel>
#include <QSettings>
#include <QSlider>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QtTest>

#include "actions/actionregistry.h"
#include "mainwindow/mainwindow.h"
#include "navigator/navigatorpanel.h"
#include "playerbar/playerbar.h"
#include "shell/libraryshell.h"
#include "shell/playbackshell.h"
#include "tracklist/tracklistpage.h"

using namespace linuxbee;
using namespace linuxbee::ui;

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
    void load(const QString &path) override { log.append(QStringLiteral("load ") + QFileInfo(path).fileName()); }
    void play() override { log.append(QStringLiteral("play")); }
    void pause() override { log.append(QStringLiteral("pause")); }
    void stop() override { log.append(QStringLiteral("stop")); }
    void seek(qint64 ms) override { log.append(QStringLiteral("seek %1").arg(ms)); }
    void setVolume(double) override { }
    void at(qint64 ms) { emit positionChanged(ms); }
    void lasts(qint64 ms) { emit durationChanged(ms); }
    QStringList take()
    {
        const QStringList out = log;
        log.clear();
        return out;
    }
};

QStringList sorted(QStringList list)
{
    list.sort();
    return list;
}

struct Rig {
    QTemporaryDir dir;
    QString a, b, c, bare;
    std::unique_ptr<Library> library;
    PlaylistStore playlists;
    QSettings settings;
    ActionRegistry actions;
    MainWindow window;
    Tape tape;
    QDateTime now = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);
    std::unique_ptr<LibraryShell> lib;
    std::unique_ptr<PlaybackShell> play;
    QSettings *use = &settings; // the settings the playback shell is given

    Rig()
        : library(Library::open(QStringLiteral(":memory:")))
        , playlists(made(QStringLiteral("playlists")))
        , settings(dir.path() + QStringLiteral("/s.ini"), QSettings::IniFormat)
        , window(&actions, &settings)
    {
        made(QStringLiteral("music"));
        a = put("basic.flac", "music/a.flac");
        b = put("basic.m4a", "music/b.m4a");
        c = put("basic.ogg", "music/c.ogg");
        bare = put("empty.flac", "music/bare.flac");
        for (const QString &p : { a, b, c, bare })
            if (library->addFile(p, QStringLiteral("Music"), now) == 0)
                QTest::qFail("a fixture did not go into the library", __FILE__, __LINE__);
        lib = std::make_unique<LibraryShell>(&window, &actions, library.get(), &playlists, &settings, ShellHooks { });
    }
    QString made(const QString &name)
    {
        QDir(dir.path()).mkpath(name);
        return dir.path() + QLatin1Char('/') + name;
    }
    QString put(const char *from, const char *to)
    {
        const QString target = dir.path() + QLatin1Char('/') + QLatin1String(to);
        if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/") + QLatin1String(from), target))
            QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
        return target;
    }
    void start()
    {
        play = std::make_unique<PlaybackShell>(
            &window, &actions, library.get(), lib.get(), &tape, use, [this] { return now; });
    }
    void run(const char16_t *id)
    {
        if (QAction *action = actions.action(s(id)))
            action->trigger();
        else
            QTest::qFail("no such action", __FILE__, __LINE__);
    }
    qint64 idOf(const QString &path) const
    {
        const auto t = library->trackByPath(path);
        return t ? t->id : 0;
    }
    QStringList queued() const
    {
        QStringList out;
        for (const PlayQueue::Entry &e : play->queue()->entries())
            out.append(e.path);
        return out;
    }
};

// Stops a test, as a failure, when the shell has not built its parts.
#define NEED_PARTS(r) \
    do { \
        if (!(r).play->queue() || !(r).play->player() || !(r).play->bar()) \
            QFAIL("the shell has not built its queue, player and player bar"); \
    } while (false)

}

class PlaybackShellTest : public QObject {
    Q_OBJECT
private slots:
    void structure();
    void activationPlays();
    void trackCommands();
    void playingTracksNode();
    void title();
    void counting();
    void errors();
    void stored();
    void gone();
};

void PlaybackShellTest::structure()
{
    // PS-BUILD
    Rig r;
    r.start();
    QVERIFY(r.play->queue());
    QVERIFY(r.play->player());
    QVERIFY(r.play->bar());
    QCOMPARE(r.window.panelWidget(MainWindow::Panel::PlayerBar), r.play->bar());
    QVERIFY(r.play->player()->state() == Player::State::Stopped);
    QCOMPARE(r.play->queue()->count(), 0);
    for (const char16_t *id : { u"track.playNow", u"track.queueNext", u"track.queueLast", u"controls.playPause", u"controls.next" })
        QVERIFY2(r.actions.isImplemented(s(id)), qPrintable(s(id)));
    QCOMPARE(r.play->lastMessage(), QString());
    QVERIFY(r.tape.log.isEmpty()); // building plays nothing
}

void PlaybackShellTest::activationPlays()
{
    // PS-PLAY: activating a track plays the list from there.
    Rig r;
    r.start();
    NEED_PARTS(r);
    const QStringList shown = r.lib->shownPaths();
    QCOMPARE(shown.size(), 4);
    emit r.lib->trackList()->trackActivated(r.idOf(shown.at(1)));
    QCOMPARE(r.queued(), shown);
    QCOMPARE(r.play->queue()->currentIndex(), 1);
    QCOMPARE(r.play->player()->currentPath(), shown.at(1));
    QVERIFY(r.play->player()->state() == Player::State::Playing);
    QCOMPARE(r.tape.take(), QStringList({ s(u"load ") + QFileInfo(shown.at(1)).fileName(), s(u"play") }));
    // The controls of the bar work on it.
    r.run(u"controls.next");
    QCOMPARE(r.play->player()->currentPath(), shown.at(2));
    emit r.tape.ended();
    QCOMPARE(r.play->player()->currentPath(), shown.at(3));
}

void PlaybackShellTest::trackCommands()
{
    // PS-COMMANDS
    Rig r;
    r.start();
    NEED_PARTS(r);
    r.run(u"track.playNow"); // nothing selected: nothing happens
    r.run(u"track.queueNext");
    r.run(u"track.queueLast");
    QCOMPARE(r.play->queue()->count(), 0);
    QVERIFY(r.tape.log.isEmpty());
    r.lib->trackList()->selectTracks({ r.idOf(r.b), r.idOf(r.c) });
    const QStringList picked = r.lib->selectedPaths();
    QCOMPARE(sorted(picked), sorted({ r.b, r.c }));
    r.run(u"track.playNow"); // only the selection, from its first
    QCOMPARE(r.queued(), picked);
    QCOMPARE(r.play->player()->currentPath(), picked.at(0));
    QVERIFY(r.play->player()->state() == Player::State::Playing);
    r.tape.take();
    r.lib->trackList()->selectTracks({ r.idOf(r.a) });
    r.run(u"track.queueLast");
    QCOMPARE(r.queued(), QStringList({ picked.at(0), picked.at(1), r.a }));
    r.lib->trackList()->selectTracks({ r.idOf(r.bare) });
    r.run(u"track.queueNext");
    QCOMPARE(r.queued(), QStringList({ picked.at(0), r.bare, picked.at(1), r.a }));
    QCOMPARE(r.play->player()->currentPath(), picked.at(0)); // queueing does not disturb what plays
    QVERIFY(r.tape.log.isEmpty());
    // Queueing with nothing playing does not start anything.
    r.play->player()->stop();
    r.play->queue()->clear();
    r.tape.take();
    r.run(u"track.queueLast");
    QCOMPARE(r.queued(), QStringList({ r.bare }));
    QVERIFY(r.play->player()->state() == Player::State::Stopped);
    QVERIFY(r.tape.log.isEmpty());
}

void PlaybackShellTest::playingTracksNode()
{
    // PS-PLAYING: the Playing Tracks node shows the queue, and follows it.
    Rig r;
    r.start();
    NEED_PARTS(r);
    QVERIFY(r.lib->navigator()->selectNode(s(u"playing")));
    QCOMPARE(r.lib->shownPaths(), QStringList());
    r.play->queue()->queueLast({ r.c, r.a });
    QCOMPARE(sorted(r.lib->shownPaths()), sorted({ r.a, r.c }));
    r.play->queue()->queueLast({ r.b });
    QCOMPARE(sorted(r.lib->shownPaths()), sorted({ r.a, r.b, r.c }));
    QCOMPARE(r.play->queue()->removeAt({ 0 }), 1);
    QCOMPARE(sorted(r.lib->shownPaths()), sorted({ r.a, r.b }));
    r.play->queue()->clear();
    QCOMPARE(r.lib->shownPaths(), QStringList());
    // Activating a track there plays what is shown (the queue is rebuilt from the list).
    r.play->queue()->queueLast({ r.a, r.b, r.c });
    const QStringList shown = r.lib->shownPaths();
    emit r.lib->trackList()->trackActivated(r.idOf(shown.at(2)));
    QCOMPARE(r.queued(), shown);
    QCOMPARE(r.play->player()->currentPath(), shown.at(2));
    QCOMPARE(r.lib->shownPaths(), shown);
}

void PlaybackShellTest::title()
{
    // PS-TITLE
    Rig r;
    r.start();
    NEED_PARTS(r);
    QCOMPARE(r.play->titleFor(r.b), s(u"M4A Artist - M4A Title"));
    QCOMPARE(r.play->titleFor(r.a), s(u"First Artist; Second Artist - Flac Title")); // every artist
    QCOMPARE(r.play->titleFor(r.bare), QString()); // no title tag
    QCOMPARE(r.play->titleFor(s(u"/not/in/the/library.flac")), QString());
    auto *label = r.play->bar()->findChild<QLabel *>(s(u"titleLabel"));
    QVERIFY(label);
    r.play->queue()->playNow({ r.b, r.bare, s(u"/not/in/the/library.flac"), r.a }, 0);
    QCOMPARE(label->text(), s(u"M4A Artist - M4A Title"));
    r.play->player()->next();
    QCOMPARE(label->text(), s(u"bare")); // the bar's own fallback: the file's name
    r.play->player()->next();
    QCOMPARE(label->text(), s(u"library"));
    r.play->player()->next();
    QCOMPARE(label->text(), s(u"First Artist; Second Artist - Flac Title"));
}

void PlaybackShellTest::counting()
{
    // PS-COUNT: what the player counts is written to the library.
    Rig r;
    r.start();
    NEED_PARTS(r);
    r.play->queue()->playNow({ r.a, r.b, s(u"/not/in/the/library.flac"), r.c }, 0);
    r.tape.lasts(100000);
    r.tape.at(99000);
    r.now = r.now.addSecs(100);
    emit r.tape.ended(); // a played to its end
    auto a = r.library->trackByPath(r.a);
    QCOMPARE(a->playCount, 1);
    QCOMPARE(a->skipCount, 0);
    QCOMPARE(a->lastPlayed, r.now);
    r.tape.lasts(100000);
    r.tape.at(3000);
    r.play->player()->next(); // b left after three seconds
    auto b = r.library->trackByPath(r.b);
    QCOMPARE(b->playCount, 0);
    QCOMPARE(b->skipCount, 1);
    r.tape.lasts(100000);
    r.tape.at(99000);
    emit r.tape.ended(); // a file the library does not know: counted nowhere, and no harm
    QCOMPARE(r.play->player()->currentPath(), r.c);
    r.tape.lasts(100000);
    r.tape.at(40000);
    r.play->player()->stop(); // neither a play nor a skip
    auto c = r.library->trackByPath(r.c);
    QCOMPARE(c->playCount, 0);
    QCOMPARE(c->skipCount, 0);
    QCOMPARE(r.library->trackByPath(r.a)->playCount, 1);
}

void PlaybackShellTest::errors()
{
    // PS-ERROR
    Rig r;
    r.start();
    NEED_PARTS(r);
    r.play->queue()->playNow({ r.a, r.b }, 0);
    emit r.tape.failed(s(u"the decoder gave up"));
    QCOMPARE(r.play->lastMessage(), s(u"Cannot play a.flac: the decoder gave up"));
    QCOMPARE(r.window.statusBar()->currentMessage(), r.play->lastMessage());
    QCOMPARE(r.library->trackByPath(r.a)->playCount, 0);
    QCOMPARE(r.library->trackByPath(r.a)->skipCount, 0);
}

void PlaybackShellTest::stored()
{
    // PS-STORE
    Rig r;
    r.start();
    NEED_PARTS(r);
    QCOMPARE(r.play->player()->volume(), 1.0);
    r.play->player()->setVolume(0.3);
    r.play->queue()->setRepeat(PlayQueue::Repeat::All);
    r.play->saveState();
    r.settings.sync();
    QCOMPARE(r.settings.value(s(u"player/volume")).toInt(), 30);
    QCOMPARE(r.settings.value(s(u"player/repeat")).toString(), s(u"all"));
    r.play.reset();
    QSettings later(nextRun(r.settings), QSettings::IniFormat); // as the next run reads it
    r.use = &later;
    r.start();
    NEED_PARTS(r);
    QCOMPARE(r.play->player()->volume(), 0.3); // restored when built
    QVERIFY(r.play->queue()->repeat() == PlayQueue::Repeat::All);
    auto *slider = r.play->bar()->findChild<QSlider *>(s(u"volumeSlider"));
    QVERIFY(slider);
    QCOMPARE(slider->value(), 30);
    QCOMPARE(r.play->queue()->count(), 0); // the queue itself is not remembered
    r.play.reset(); // before `later` goes
}

void PlaybackShellTest::gone()
{
    // Destroying the shell stops the sound, empties the bar's panel and frees the commands.
    Rig r;
    r.start();
    NEED_PARTS(r);
    r.play->queue()->playNow({ r.a }, 0);
    r.tape.take();
    QPointer<PlayerBar> bar(r.play->bar());
    r.play.reset();
    QVERIFY(bar.isNull());
    QVERIFY(r.window.panelWidget(MainWindow::Panel::PlayerBar) == nullptr);
    QVERIFY(!r.actions.isImplemented(s(u"track.playNow")));
    QVERIFY(!r.actions.isImplemented(s(u"controls.playPause")));
    QCOMPARE(r.tape.take(), QStringList({ s(u"stop") }));
    // The library shell lives on and may still report an activation.
    emit r.lib->trackList()->trackActivated(r.idOf(r.a));
    QVERIFY(r.tape.log.isEmpty());
}

QTEST_MAIN(PlaybackShellTest)
#include "tst_ui_playbackshell.moc"
