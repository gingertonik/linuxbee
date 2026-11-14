// LOCKED ACCEPTANCE TEST (an auto-playlist plays). Written by the reviewer, not the builder.
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
#include "playlists/autoplayliststore.h"
#include "shell/autoplaylistnodes.h"
#include "shell/libraryshell.h"
#include "shell/playbackshell.h"
#include "tracklist/tracklistmodel.h"
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

AutoPlaylist titled(const char16_t *part)
{
    AutoPlaylist p;
    Rule rule;
    rule.field = s(u"Title");
    rule.op = RuleOperator::Contains;
    rule.values = { s(part) };
    p.rules.rules = { rule };
    return p;
}

}

class PlaybackShellAutoTest : public QObject {
    Q_OBJECT
private slots:
    void anAutoPlaylistPlaysWhatTheListShows();
    void whatIsNotPlayed();
};

void PlaybackShellAutoTest::anAutoPlaylistPlaysWhatTheListShows()
{
    // PS-NODE: activating an auto-playlist plays the tracks the list shows, in the order shown.
    (void)&nextRun; // a helper of the rig this file shares with the plan's own test
    Rig r;
    AutoPlaylistStore store(r.dir.path() + s(u"/auto"));
    QVERIFY(store.save(s(u"Titles"), titled(u"title"))); // a, b and c have a title; `bare` has none
    AutoPlaylistNodes nodes(&r.window, r.lib.get(), r.library.get(), &store);
    r.start();
    NEED_PARTS(r);
    QCOMPARE(r.lib->currentNode(), s(u"library:Music"));
    r.tape.take();

    emit r.lib->navigator()->nodeActivated(s(u"auto:Titles"));
    QCOMPARE(r.lib->currentNode(), s(u"auto:Titles")); // it is on show as well
    QCOMPARE(sorted(r.lib->shownPaths()), sorted({ r.a, r.b, r.c }));
    QCOMPARE(r.queued(), r.lib->shownPaths());
    QCOMPARE(r.play->queue()->currentIndex(), 0);
    QCOMPARE(r.play->player()->currentPath(), r.lib->shownPaths().value(0));
    QVERIFY(r.play->player()->state() == Player::State::Playing);

    // The list sorted another way: that order is played. (Ogg Title, M4A Title, Flac Title.)
    TrackListModel *model = r.lib->model();
    const int title = int(model->columns().indexOf(s(u"Title")));
    QVERIFY(title >= 0);
    model->sort(title, Qt::DescendingOrder);
    QCOMPARE(r.lib->shownPaths(), QStringList({ r.c, r.b, r.a }));
    r.play->player()->stop();
    emit r.lib->navigator()->nodeActivated(s(u"auto:Titles"));
    QCOMPARE(r.queued(), QStringList({ r.c, r.b, r.a }));
    QCOMPARE(r.play->player()->currentPath(), r.c);
    model->sort(title, Qt::AscendingOrder);
    r.play->player()->stop();
    emit r.lib->navigator()->nodeActivated(s(u"auto:Titles"));
    QCOMPARE(r.queued(), QStringList({ r.a, r.b, r.c }));

    // Narrowed by the search box: what is shown is what is played.
    r.lib->trackList()->setSearchText(s(u"m4a"));
    QCOMPARE(r.lib->shownPaths(), QStringList({ r.b }));
    r.play->player()->stop();
    emit r.lib->navigator()->nodeActivated(s(u"auto:Titles"));
    QCOMPARE(r.queued(), QStringList({ r.b }));
    QVERIFY(r.play->player()->state() == Player::State::Playing);
}

void PlaybackShellAutoTest::whatIsNotPlayed()
{
    // PS-NODE: an auto-playlist that shows nothing, and one that is not there.
    Rig r;
    AutoPlaylistStore store(r.dir.path() + s(u"/auto"));
    QVERIFY(store.save(s(u"Nothing"), titled(u"no such words anywhere")));
    QVERIFY(store.save(s(u"Titles"), titled(u"title")));
    AutoPlaylistNodes nodes(&r.window, r.lib.get(), r.library.get(), &store);
    r.start();
    NEED_PARTS(r);
    emit r.lib->navigator()->nodeActivated(s(u"auto:Titles"));
    const QStringList playing = r.queued();
    QCOMPARE(playing.size(), 3);
    const QString current = r.play->player()->currentPath();

    emit r.lib->navigator()->nodeActivated(s(u"auto:Nothing"));
    QCOMPARE(r.lib->currentNode(), s(u"auto:Nothing")); // chosen, and nothing to play
    QCOMPARE(r.lib->shownPaths(), QStringList());
    QCOMPARE(r.queued(), playing); // the queue is left alone
    QCOMPARE(r.play->player()->currentPath(), current);

    emit r.lib->navigator()->nodeActivated(s(u"auto:Not There"));
    QCOMPARE(r.lib->currentNode(), s(u"auto:Nothing"));
    QCOMPARE(r.queued(), playing);
    emit r.lib->navigator()->nodeActivated(s(u"auto:"));
    QCOMPARE(r.queued(), playing);
}

QTEST_MAIN(PlaybackShellAutoTest)
#include "tst_ui_playbackshell_auto.moc"
