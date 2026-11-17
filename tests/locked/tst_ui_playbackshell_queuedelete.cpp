// LOCKED ACCEPTANCE TEST (in the Playing Tracks view the Delete key takes the track out of the queue). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/library-shell.md (SH-REMOVE) and docs/spec/playback-shell.md (PS-UNQUEUE).
// Offscreen, with the real window, library, shells and queue; the player's backend is a stand-in.
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
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
    QString a, b, c, e;
    std::unique_ptr<Library> library;
    PlaylistStore playlists;
    QSettings settings;
    ActionRegistry actions;
    MainWindow window;
    Tape tape;
    QDateTime now = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);
    std::unique_ptr<LibraryShell> lib;
    std::unique_ptr<PlaybackShell> play;
    bool yes = true;
    int asked = 0;

    Rig()
        : library(Library::open(QStringLiteral(":memory:")))
        , playlists(made(QStringLiteral("playlists")))
        , settings(dir.path() + QStringLiteral("/s.ini"), QSettings::IniFormat)
        , window(&actions, &settings)
    {
        qputenv("XDG_DATA_HOME", QFile::encodeName(dir.path() + QStringLiteral("/xdg-data"))); // a trash of this test's own
        (void)&nextRun;
        (void)&sorted;
        made(QStringLiteral("music"));
        a = put("basic.flac", "music/a.flac");
        b = put("basic.m4a", "music/b.m4a");
        c = put("basic.ogg", "music/c.ogg");
        e = put("basic.mp3", "music/e.mp3");
        for (const QString &p : { a, b, c, e })
            if (library->addFile(p, QStringLiteral("Music"), now) == 0)
                QTest::qFail("a fixture did not go into the library", __FILE__, __LINE__);
        ShellHooks hooks;
        hooks.now = [this] { return now; };
        hooks.confirm = [this](const QString &) {
            ++asked;
            return yes;
        };
        lib = std::make_unique<LibraryShell>(&window, &actions, library.get(), &playlists, &settings, hooks);
        play = std::make_unique<PlaybackShell>(&window, &actions, library.get(), lib.get(), &tape, &settings, [this] { return now; });
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
    void deleteFiles(const QStringList &paths)
    {
        QList<qint64> ids;
        for (const QString &p : paths) {
            const auto t = library->trackByPath(p);
            ids.append(t ? t->id : 0);
        }
        lib->trackList()->selectTracks(ids);
        if (QAction *action = actions.action(QStringLiteral("file.delete")))
            action->trigger();
        else
            QTest::qFail("no such action", __FILE__, __LINE__);
    }
    QStringList queued() const
    {
        QStringList out;
        for (const PlayQueue::Entry &entry : play->queue()->entries())
            out.append(entry.path);
        return out;
    }
};

#define NEED_PARTS(r) \
    do { \
        if (!(r).play->queue() || !(r).play->player()) \
            QFAIL("the shell has not built its queue and player"); \
    } while (false)

void removeKey(Rig &r, const QStringList &paths)
{
    QList<qint64> ids;
    for (const QString &p : paths) {
        const auto t = r.library->trackByPath(p);
        ids.append(t ? t->id : 0);
    }
    r.lib->trackList()->selectTracks(ids);
    if (QAction *action = r.actions.action(QStringLiteral("file.remove")))
        action->trigger();
    else
        QTest::qFail("no such action", __FILE__, __LINE__);
}

QString label(Rig &r)
{
    const QAction *action = r.actions.action(QStringLiteral("file.remove"));
    return action ? action->text() : QStringLiteral("NO SUCH ACTION");
}

}

class PlaybackShellQueueDeleteTest : public QObject {
    Q_OBJECT
private slots:
    void outOfTheQueue();
    void withNothingPlaying();
    void elsewhereTheQueueIsLeftAlone();
};

void PlaybackShellQueueDeleteTest::outOfTheQueue()
{
    // SH-REMOVE, PS-UNQUEUE: in Playing Tracks the Delete key takes the selected tracks out of the queue.
    // It asks nothing; the library and the files are not touched; the track playing stays.
    Rig r;
    NEED_PARTS(r);
    r.play->queue()->playNow({ r.a, r.b, r.c, r.e, r.c }, 1); // b is current; c is there twice
    QVERIFY(r.lib->navigator()->selectNode(s(u"playing")));
    QCOMPARE(label(r), s(u"Remove from Queue"));
    QSignalSpy gone(r.library.get(), &Library::tracksRemoved);
    QSignalSpy changed(r.play->queue(), &PlayQueue::entriesChanged);
    // Nothing selected: nothing happens and nothing is said.
    const QString before = r.play->lastMessage();
    removeKey(r, { });
    QCOMPARE(r.queued().size(), 5);
    QCOMPARE(r.play->lastMessage(), before);

    removeKey(r, { r.a, r.c });
    QCOMPARE(r.queued(), QStringList({ r.b, r.e })); // both entries of c
    QCOMPARE(changed.count(), 1); // one change of the queue
    QCOMPARE(r.play->queue()->currentPath(), r.b);
    QCOMPARE(r.asked, 0);
    QCOMPARE(gone.count(), 0);
    QCOMPARE(r.library->count(), 4);
    for (const QString &p : { r.a, r.b, r.c, r.e })
        QVERIFY2(QFileInfo::exists(p), qPrintable(p));
    QCOMPARE(r.play->lastMessage(), s(u"Removed 3 from the queue")); // entries, not tracks
    QCOMPARE(r.window.statusBar()->currentMessage(), r.play->lastMessage());
    QCOMPARE(r.lib->shownPaths(), QStringList({ r.b, r.e })); // the view follows the queue

    // The playing track among the selected: the others go, it stays, and the message says so.
    removeKey(r, { r.b, r.e });
    QCOMPARE(r.queued(), QStringList({ r.b }));
    QCOMPARE(r.play->queue()->currentPath(), r.b);
    QCOMPARE(r.play->lastMessage(), s(u"Removed 1 from the queue; the playing track stays"));
    // The playing track alone.
    changed.clear();
    removeKey(r, { r.b });
    QCOMPARE(r.queued(), QStringList({ r.b }));
    QCOMPARE(changed.count(), 0);
    QCOMPARE(r.play->lastMessage(), s(u"The playing track stays in the queue"));
    QCOMPARE(r.asked, 0);
    QCOMPARE(r.library->count(), 4);
}

void PlaybackShellQueueDeleteTest::withNothingPlaying()
{
    // PS-UNQUEUE: with no current entry, every selected track goes.
    Rig r;
    NEED_PARTS(r);
    r.play->queue()->queueLast({ r.a, r.b, r.c });
    QCOMPARE(r.play->queue()->currentIndex(), -1);
    QVERIFY(r.lib->navigator()->selectNode(s(u"playing")));
    QCOMPARE(r.lib->shownPaths(), QStringList({ r.a, r.b, r.c }));
    removeKey(r, { r.b });
    QCOMPARE(r.queued(), QStringList({ r.a, r.c }));
    QCOMPARE(r.play->lastMessage(), s(u"Removed 1 from the queue"));
    removeKey(r, { r.a, r.c });
    QCOMPARE(r.queued(), QStringList());
    QCOMPARE(r.play->lastMessage(), s(u"Removed 2 from the queue"));
    QCOMPARE(r.lib->shownPaths(), QStringList());
    QCOMPARE(r.library->count(), 4);
}

void PlaybackShellQueueDeleteTest::elsewhereTheQueueIsLeftAlone()
{
    // SH-REMOVE, PS-TRASHED: in a library section the key is Remove from Library, asks, and leaves the queue alone.
    Rig r;
    NEED_PARTS(r);
    r.play->queue()->playNow({ r.a, r.b, r.c }, 0);
    QVERIFY(r.lib->navigator()->selectNode(s(u"library:Music")));
    QCOMPARE(label(r), s(u"Remove from Library"));
    const QString before = r.play->lastMessage();
    removeKey(r, { r.b });
    QCOMPARE(r.asked, 1);
    QCOMPARE(r.library->count(), 3);
    QCOMPARE(r.queued(), QStringList({ r.a, r.b, r.c }));
    QCOMPARE(r.play->lastMessage(), before);
    // In a playlist: out of the playlist, and the queue is left alone too.
    const QString id = r.playlists.create(s(u"Mix"));
    QVERIFY(r.playlists.setTracks(id, { r.a, r.c }));
    QVERIFY(r.lib->navigator()->selectNode(s(u"playlist:") + id));
    QCOMPARE(label(r), s(u"Remove from Playlist"));
    removeKey(r, { r.c });
    QCOMPARE(r.playlists.tracks(id), QStringList({ r.a }));
    QCOMPARE(r.queued(), QStringList({ r.a, r.b, r.c }));
    QVERIFY(r.lib->navigator()->selectNode(s(u"playing")));
    QCOMPARE(label(r), s(u"Remove from Queue"));
    QVERIFY(r.lib->navigator()->selectNode(s(u"library:Inbox")));
    QCOMPARE(label(r), s(u"Remove from Library"));
}

QTEST_MAIN(PlaybackShellQueueDeleteTest)
#include "tst_ui_playbackshell_queuedelete.moc"
