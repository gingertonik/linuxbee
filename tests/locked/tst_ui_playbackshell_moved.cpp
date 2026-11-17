// LOCKED ACCEPTANCE TEST (the playback shell: a file the program moved goes on playing and is counted). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/playback-shell.md (PS-MOVED) and docs/spec/player.md (X-RENAME).
// The backend is a stand-in that records what it is told.
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

class PlaybackShellMovedTest : public QObject {
    Q_OBJECT
private slots:
    void thePlayingFileMoves();
};

void PlaybackShellMovedTest::thePlayingFileMoves()
{
    // PS-MOVED: the library says a path moved; the queue's entries are renamed and the player goes on.
    Rig r;
    NEED_PARTS(r);
    r.play->queue()->playNow({ r.a, r.b, r.a }, 0);
    QCOMPARE(r.play->player()->currentPath(), r.a);
    r.tape.lasts(10000);
    r.tape.at(1000);
    r.tape.take();
    const qint64 id = r.library->trackByPath(r.a)->id;
    const int playsBefore = r.library->track(id)->playCount;
    QSignalSpy trackChanged(r.play->player(), &Player::trackChanged);
    QSignalSpy counted(r.play->player(), &Player::playCounted);

    const QString moved = r.dir.path() + QStringLiteral("/music/renamed.flac");
    QVERIFY(QFile::rename(r.a, moved));
    QVERIFY(r.library->setPath(id, moved));

    QCOMPARE(r.queued(), QStringList({ moved, r.b, moved }));
    QCOMPARE(r.play->player()->currentPath(), moved); // X-RENAME
    QVERIFY(r.play->player()->state() == Player::State::Playing);
    QCOMPARE(r.tape.take(), QStringList()); // the backend was told nothing: the open file is the same file
    QCOMPARE(trackChanged.count(), 0); // it is the same track
    // It plays to its end and the play is counted for the track, under its new path.
    for (qint64 ms = 2000; ms <= 9000; ms += 1000)
        r.tape.at(ms);
    emit r.tape.ended();
    QCOMPARE(counted.count(), 1);
    QCOMPARE(counted.at(0).at(0).toString(), moved);
    QCOMPARE(r.library->track(id)->playCount, playsBefore + 1);
    // The next entry started as usual, and the third, the same file again, is loaded from its new place.
    QCOMPARE(r.play->player()->currentPath(), r.b);
    r.tape.take();
    r.play->player()->next();
    QCOMPARE(r.play->player()->currentPath(), moved);
    QVERIFY(r.tape.take().contains(s(u"load renamed.flac")));
}

QTEST_MAIN(PlaybackShellMovedTest)
#include "tst_ui_playbackshell_moved.moc"
