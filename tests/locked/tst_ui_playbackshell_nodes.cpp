// LOCKED ACCEPTANCE TEST (activating a playlist). Written by the reviewer, not the builder.
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

class PlaybackShellNodesTest : public QObject {
    Q_OBJECT
private slots:
    void aPlaylistPlaysInItsOwnOrder();
    void whatIsNotPlayed();
    void gone();
};

void PlaybackShellNodesTest::aPlaylistPlaysInItsOwnOrder()
{
    // PS-NODE: activating a playlist plays it, in the playlist's order, from its first track.
    Rig r;
    const QString mix = r.playlists.create(s(u"Mix"));
    const QString back = r.playlists.create(s(u"Back"));
    QVERIFY(!mix.isEmpty() && !back.isEmpty());
    // A path the library does not know is passed over; a track is played once (SH-NODE).
    QCOMPARE(r.playlists.append(mix, { r.c, r.a, s(u"/nowhere/x.mp3"), r.bare, r.c }, true), 5);
    QCOMPARE(r.playlists.append(back, { r.bare, r.b, r.a }), 3);
    r.start();
    NEED_PARTS(r);
    QCOMPARE(r.lib->currentNode(), s(u"library:Music"));
    r.tape.take();

    emit r.lib->navigator()->nodeActivated(s(u"playlist:") + mix);
    QCOMPARE(r.lib->currentNode(), s(u"playlist:") + mix); // it is on show as well
    QCOMPARE(r.queued(), QStringList({ r.c, r.a, r.bare }));
    QCOMPARE(r.play->queue()->currentIndex(), 0);
    QCOMPARE(r.play->player()->currentPath(), r.c);
    QVERIFY(r.play->player()->state() == Player::State::Playing);
    QCOMPARE(r.tape.take(), QStringList({ s(u"load c.ogg"), s(u"play") }));

    // The other one, whose order no sorting of the list gives if it gave the first.
    emit r.lib->navigator()->nodeActivated(s(u"playlist:") + back);
    QCOMPARE(r.lib->currentNode(), s(u"playlist:") + back);
    QCOMPARE(r.queued(), QStringList({ r.bare, r.b, r.a }));
    QCOMPARE(r.play->player()->currentPath(), r.bare);

    // The node already on show, with the list narrowed by a search: the whole playlist still.
    r.lib->trackList()->setSearchText(s(u"no such words anywhere"));
    QCOMPARE(r.lib->shownPaths(), QStringList());
    r.play->player()->stop();
    emit r.lib->navigator()->nodeActivated(s(u"playlist:") + back);
    QCOMPARE(r.queued(), QStringList({ r.bare, r.b, r.a }));
    QVERIFY(r.play->player()->state() == Player::State::Playing);
}

void PlaybackShellNodesTest::whatIsNotPlayed()
{
    // PS-NODE: only playlists, and only with something in them.
    Rig r;
    const QString empty = r.playlists.create(s(u"Empty"));
    const QString strangers = r.playlists.create(s(u"Strangers"));
    QCOMPARE(r.playlists.append(strangers, { s(u"/nowhere/x.mp3") }), 1);
    r.start();
    NEED_PARTS(r);
    r.play->queue()->playNow({ r.b }, 0);
    r.tape.take();
    for (const QString &node : { s(u"library:Music"), s(u"library:Inbox"), s(u"playing"), s(u"playlist:") + empty,
             s(u"playlist:") + strangers, s(u"playlist:no-such-playlist"), QString() }) {
        emit r.lib->navigator()->nodeActivated(node);
        QVERIFY2(r.queued() == QStringList({ r.b }), qPrintable(node));
        QVERIFY2(r.tape.take().isEmpty(), qPrintable(node));
    }
    QCOMPARE(r.play->player()->currentPath(), r.b);
}

void PlaybackShellNodesTest::gone()
{
    // PS-BUILD: with the playback shell destroyed, activating goes nowhere.
    (void)&nextRun; // helpers of the rig this file shares with the plan's own test, not needed here
    (void)&sorted;
    Rig r;
    const QString mix = r.playlists.create(s(u"Mix"));
    QCOMPARE(r.playlists.append(mix, { r.a }), 1);
    r.start();
    NEED_PARTS(r);
    r.play.reset();
    r.tape.take();
    emit r.lib->navigator()->nodeActivated(s(u"playlist:") + mix);
    QVERIFY(r.tape.take().isEmpty()); // nothing after the shell went
    QCOMPARE(r.lib->currentNode(), s(u"library:Music"));
}

QTEST_MAIN(PlaybackShellNodesTest)
#include "tst_ui_playbackshell_nodes.moc"
