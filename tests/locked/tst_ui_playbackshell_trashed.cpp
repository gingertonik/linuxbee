// LOCKED ACCEPTANCE TEST (files that went to the trash leave the queue, but the one playing). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/play-queue.md (Q-EDIT, removePaths),
// docs/spec/library-shell.md (SH-DELETE) and docs/spec/playback-shell.md
// (PS-TRASHED). Offscreen, with the real library, shells, queue and player,
// over a backend that only writes down what it is told. The trash is a
// folder of this test's own.
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
        hooks.confirm = [this](const QString &) { return yes; };
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

}

class PlaybackShellTrashedTest : public QObject {
    Q_OBJECT
private slots:
    void theQueueItself();
    void theShellSaysWhatWent();
    void theyLeaveTheQueue();
    void theOnePlayingStays();
};

void PlaybackShellTrashedTest::theQueueItself()
{
    // Q-EDIT, removePaths: every entry with one of the paths goes, but the current one. One change.
    PlayQueue q;
    q.playNow({ s(u"/m/1"), s(u"/m/2"), s(u"/m/3"), s(u"/m/2"), s(u"/m/4"), s(u"/m/3") }, 2); // current: /m/3, the first of two
    QSignalSpy changed(&q, &PlayQueue::entriesChanged);
    QSignalSpy current(&q, &PlayQueue::currentChanged);
    const qint64 keptId = q.entries().at(2).entryId;
    QCOMPARE(q.removePaths({ s(u"/m/2"), s(u"/m/3"), s(u"/m/9"), s(u"/m/2") }), 3);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(q.count(), 3);
    QCOMPARE(q.entries().at(0).path, s(u"/m/1"));
    QCOMPARE(q.entries().at(1).path, s(u"/m/3")); // the one playing: kept; the other /m/3 went
    QCOMPARE(q.entries().at(1).entryId, keptId);
    QCOMPARE(q.entries().at(2).path, s(u"/m/4"));
    QCOMPARE(q.currentIndex(), 1);
    QCOMPARE(q.currentPath(), s(u"/m/3"));
    QCOMPARE(q.next(false), 2); // and Next goes on to what is left
    // Nothing of them in the list: nothing changes and nothing is said.
    changed.clear();
    current.clear();
    QCOMPARE(q.removePaths({ s(u"/m/9") }), 0);
    QCOMPARE(q.removePaths({ }), 0);
    QCOMPARE(q.removePaths({ s(u"/m/4") }), 0); // current now: it stays
    QCOMPARE(changed.count(), 0);
    QCOMPARE(current.count(), 0);
    QCOMPARE(q.count(), 3);
    // A path is compared as it is written, the way the queue holds it.
    QCOMPARE(q.removePaths({ s(u"/m/../m/1"), s(u"/M/1") }), 0);
    // No current entry: all of them go.
    PlayQueue idle;
    idle.queueLast({ s(u"/m/1"), s(u"/m/2"), s(u"/m/1") });
    QCOMPARE(idle.currentIndex(), -1);
    QCOMPARE(idle.removePaths({ s(u"/m/1") }), 2);
    QCOMPARE(idle.count(), 1);
    // Shuffle: what went is not come back to.
    PlayQueue mixed;
    QStringList many;
    for (int i = 0; i < 30; ++i)
        many.append(QStringLiteral("/m/%1").arg(i));
    mixed.playNow(many, 0);
    mixed.setShuffle(true, 7);
    QStringList out;
    for (int i = 1; i < 30; i += 2)
        out.append(many.at(i));
    QCOMPARE(mixed.removePaths(out), 15);
    QSet<QString> heard { mixed.currentPath() };
    while (mixed.next(true) >= 0)
        heard.insert(mixed.currentPath());
    QCOMPARE(heard.size(), 15);
    for (const QString &p : out)
        QVERIFY2(!heard.contains(p), qPrintable(p));
}

void PlaybackShellTrashedTest::theShellSaysWhatWent()
{
    // SH-DELETE: filesTrashed, once, with the paths as the library held them, in the order shown; not on "no", not for none.
    Rig r;
    QSignalSpy went(r.lib.get(), &LibraryShell::filesTrashed);
    r.yes = false;
    r.deleteFiles({ r.a });
    QCOMPARE(went.count(), 0);
    r.yes = true;
    QVERIFY(QFile::remove(r.c)); // not there any more: it does not go to the trash
    const QStringList shown = r.lib->shownPaths();
    r.deleteFiles({ r.a, r.c, r.e });
    QCOMPARE(went.count(), 1);
    QStringList want;
    for (const QString &p : shown)
        if (p == r.a || p == r.e)
            want.append(p);
    QCOMPARE(went.at(0).at(0).toStringList(), want);
    QVERIFY(!QFileInfo::exists(r.a));
    went.clear();
    r.deleteFiles({ r.c }); // nothing goes
    QCOMPARE(went.count(), 0);
}

void PlaybackShellTrashedTest::theyLeaveTheQueue()
{
    // PS-TRASHED: files that went leave the queue; what is playing is not disturbed.
    Rig r;
    NEED_PARTS(r);
    const QStringList shown = r.lib->shownPaths();
    QCOMPARE(shown.size(), 4);
    r.play->queue()->playNow(shown, 0);
    r.play->queue()->queueLast({ shown.at(2) }); // a second time in the list
    QCOMPARE(r.play->player()->currentPath(), shown.at(0));
    r.tape.take();
    QSignalSpy changed(r.play->queue(), &PlayQueue::entriesChanged);
    r.deleteFiles({ shown.at(2), shown.at(3) });
    QVERIFY(!QFileInfo::exists(shown.at(2)));
    QCOMPARE(r.queued(), QStringList({ shown.at(0), shown.at(1) }));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(r.play->queue()->currentIndex(), 0);
    QVERIFY(r.play->player()->state() == Player::State::Playing);
    QCOMPARE(r.tape.take(), QStringList()); // the player was told nothing
    // The Playing Tracks node follows (PS-PLAYING), as for any change of the queue.
    r.lib->navigator()->selectNode(s(u"playing"));
    QCOMPARE(r.lib->shownPaths(), QStringList({ shown.at(0), shown.at(1) }));
    // A file the trash did not take stays in the queue: its track is still in the library.
    r.lib->navigator()->selectNode(s(u"library:Music"));
    QVERIFY(QFile::remove(shown.at(1)));
    r.deleteFiles({ shown.at(1) });
    QCOMPARE(r.queued(), QStringList({ shown.at(0), shown.at(1) }));
}

void PlaybackShellTrashedTest::theOnePlayingStays()
{
    // PS-TRASHED: the track playing stays in the queue and plays on; Next leaves it behind.
    Rig r;
    NEED_PARTS(r);
    const QStringList shown = r.lib->shownPaths();
    r.play->queue()->playNow(shown, 1);
    r.tape.take();
    r.deleteFiles({ shown.at(1), shown.at(2) });
    QVERIFY(!QFileInfo::exists(shown.at(1)));
    QCOMPARE(r.queued(), QStringList({ shown.at(0), shown.at(1), shown.at(3) }));
    QCOMPARE(r.play->queue()->currentIndex(), 1);
    QCOMPARE(r.play->player()->currentPath(), shown.at(1));
    QVERIFY(r.play->player()->state() == Player::State::Playing);
    QCOMPARE(r.tape.take(), QStringList());
    if (QAction *next = r.actions.action(s(u"controls.next")))
        next->trigger();
    QCOMPARE(r.play->player()->currentPath(), shown.at(3));
}

QTEST_MAIN(PlaybackShellTrashedTest)
#include "tst_ui_playbackshell_trashed.moc"
