// LOCKED ACCEPTANCE TEST (the playback shell applies ReplayGain on playback). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/playback-shell.md (PS-GAIN), with the factor of docs/spec/replay-gain.md (N-APPLY)
// and the volume rule of docs/spec/player.md (X-VOLUME). The backend is a stand-in that records what it is told.
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
#include "tags/tagwriter.h"

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
    QList<double> volumes;
    void setVolume(double v) override { volumes.append(v); }
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

// Puts ReplayGain tags in a file and has the library read it again. An empty text leaves that tag out.
void gain(Rig &r, const QString &path, const char *trackGain, const char *trackPeak, const char *albumGain, const char *albumPeak)
{
    TagChanges c;
    const auto put = [&c](const char16_t *key, const char *value) {
        c.other.insert(s(key), *value ? QStringList { QString::fromLatin1(value) } : QStringList { });
    };
    put(u"REPLAYGAIN_TRACK_GAIN", trackGain);
    put(u"REPLAYGAIN_TRACK_PEAK", trackPeak);
    put(u"REPLAYGAIN_ALBUM_GAIN", albumGain);
    put(u"REPLAYGAIN_ALBUM_PEAK", albumPeak);
    if (!writeTags(path, c).ok())
        QTest::qFail("could not write the gain tags", __FILE__, __LINE__);
    const auto t = r.library->trackByPath(path);
    if (!t || !r.library->refresh(t->id))
        QTest::qFail("the library did not read the file again", __FILE__, __LINE__);
}

bool checked(Rig &r, const char16_t *id)
{
    const QAction *action = r.actions.action(s(id));
    return action && action->isChecked();
}

void choose(Rig &r, const char16_t *id)
{
    if (QAction *action = r.actions.action(s(id)))
        action->trigger();
    else
        QTest::qFail("no such action", __FILE__, __LINE__);
}

// What the backend was last told, to three places.
QString vol(const Rig &r)
{
    return r.tape.volumes.isEmpty() ? QStringLiteral("never told") : QString::number(r.tape.volumes.last(), 'f', 3);
}

// -6.0206 dB is a factor of one half; -12.0412 of one quarter; +6.0206 of two.
void usualTags(Rig &r)
{
    gain(r, r.a, "-6.0206 dB", "0.9", "-12.0412 dB", "0.9"); // both
    gain(r, r.c, "", "", "-12.0412 dB", "0.5"); // an album gain only
    gain(r, r.e, "+6.0206 dB", "0.25", "", ""); // louder, with room for it
    // b has none.
}

}

class PlaybackShellGainTest : public QObject {
    Q_OBJECT
private slots:
    void offUntilChosen();
    void theModes();
    void rememberedForTheNextRun();
    void neverAboveFull();
    void noReadForAVolumeChange();
};

void PlaybackShellGainTest::offUntilChosen()
{
    // PS-GAIN: the three commands are there, one is checked, and with Off nothing is changed.
    Rig r;
    NEED_PARTS(r);
    usualTags(r);
    for (const char16_t *id : { u"controls.gain.off", u"controls.gain.track", u"controls.gain.album" })
        QVERIFY2(r.actions.isImplemented(s(id)), qPrintable(s(id)));
    QVERIFY(checked(r, u"controls.gain.off"));
    QVERIFY(!checked(r, u"controls.gain.track"));
    QVERIFY(!checked(r, u"controls.gain.album"));
    r.play->queue()->playNow({ r.a, r.b }, 0);
    QCOMPARE(r.play->player()->currentPath(), r.a);
    QCOMPARE(vol(r), s(u"1.000"));
}

void PlaybackShellGainTest::theModes()
{
    // PS-GAIN with N-APPLY: the factor of the track playing, from the tags the library holds, applied at once
    // when the mode changes and for each track as it starts.
    Rig r;
    NEED_PARTS(r);
    usualTags(r);
    r.play->queue()->playNow({ r.a, r.c, r.b }, 0);
    choose(r, u"controls.gain.track");
    QVERIFY(checked(r, u"controls.gain.track"));
    QVERIFY(!checked(r, u"controls.gain.off") && !checked(r, u"controls.gain.album")); // one at a time
    QCOMPARE(vol(r), s(u"0.500")); // a, by its track gain; told while it plays
    choose(r, u"controls.gain.album");
    QVERIFY(checked(r, u"controls.gain.album") && !checked(r, u"controls.gain.track"));
    QCOMPARE(vol(r), s(u"0.250")); // a, by its album gain
    r.play->player()->next();
    QCOMPARE(r.play->player()->currentPath(), r.c);
    QCOMPARE(vol(r), s(u"0.250")); // c has an album gain
    choose(r, u"controls.gain.track");
    QCOMPARE(vol(r), s(u"0.250")); // c has no track gain: the album gain stands in (N-APPLY 2)
    r.play->player()->next();
    QCOMPARE(r.play->player()->currentPath(), r.b);
    QCOMPARE(vol(r), s(u"1.000")); // b has no gain at all
    // The user's own volume multiplies it.
    r.play->player()->previous();
    r.play->player()->previous();
    QCOMPARE(r.play->player()->currentPath(), r.a);
    r.play->player()->setVolume(0.5);
    QCOMPARE(vol(r), s(u"0.250"));
    choose(r, u"controls.gain.off");
    QCOMPARE(vol(r), s(u"0.500"));
    // Choosing the mode that is on changes nothing and leaves it checked.
    choose(r, u"controls.gain.off");
    QVERIFY(checked(r, u"controls.gain.off"));
    // The tags as the library has them now: after a change and a refresh, the next start uses the new value.
    choose(r, u"controls.gain.track");
    r.play->player()->setVolume(1.0);
    gain(r, r.b, "-6.0206 dB", "0.9", "", "");
    r.play->queue()->playNow({ r.b }, 0);
    QCOMPARE(vol(r), s(u"0.500"));
    // A file the library does not know: no change.
    const QString stranger = r.put("basic.flac", "music/stranger.flac");
    r.play->queue()->playNow({ stranger }, 0);
    QCOMPARE(vol(r), s(u"1.000"));
}

void PlaybackShellGainTest::rememberedForTheNextRun()
{
    // PS-GAIN: the mode is stored when it is chosen, and the next shell starts with it.
    Rig r;
    NEED_PARTS(r);
    usualTags(r);
    choose(r, u"controls.gain.album");
    QSettings later(nextRun(r.settings), QSettings::IniFormat);
    QCOMPARE(later.value(s(u"playback/gain")).toString(), s(u"album"));
    r.play.reset();
    QVERIFY(!r.actions.isImplemented(s(u"controls.gain.album"))); // PS-BUILD: the commands are freed with the shell
    Tape second;
    r.play = std::make_unique<PlaybackShell>(&r.window, &r.actions, r.library.get(), r.lib.get(), &second, &r.settings, [&r] { return r.now; });
    QVERIFY(checked(r, u"controls.gain.album"));
    QVERIFY(!checked(r, u"controls.gain.off"));
    r.play->queue()->playNow({ r.a }, 0);
    QCOMPARE(second.volumes.isEmpty() ? -1.0 : qRound(second.volumes.last() * 1000) / 1000.0, 0.25);
    // A stored value that is none of the three is Off.
    r.play.reset();
    r.settings.setValue(s(u"playback/gain"), s(u"loud"));
    Tape third;
    r.play = std::make_unique<PlaybackShell>(&r.window, &r.actions, r.library.get(), r.lib.get(), &third, &r.settings, [&r] { return r.now; });
    QVERIFY(checked(r, u"controls.gain.off"));
    r.play.reset(); // before `third` goes
}

void PlaybackShellGainTest::neverAboveFull()
{
    // X-VOLUME holds: a gain that would make a track louder uses the room the user's volume leaves, and no more.
    Rig r;
    NEED_PARTS(r);
    usualTags(r);
    choose(r, u"controls.gain.track");
    r.play->player()->setVolume(0.4);
    r.play->queue()->playNow({ r.e }, 0);
    QCOMPARE(vol(r), s(u"0.800")); // 0.4 times two
    r.play->player()->setVolume(1.0);
    QCOMPARE(vol(r), s(u"1.000")); // held at full
    // N-APPLY 5: the peak limits the factor. A peak of 0.8 allows 1.25, not 2.
    gain(r, r.e, "+6.0206 dB", "0.8", "", "");
    r.play->player()->setVolume(0.4);
    r.play->queue()->playNow({ r.a }, 0);
    r.play->queue()->playNow({ r.e }, 0);
    QCOMPARE(vol(r), s(u"0.500")); // 0.4 times 1.25
}

void PlaybackShellGainTest::noReadForAVolumeChange()
{
    // PS-GAIN, speed: moving the volume asks the library nothing. A track starting costs at most one read more
    // than it does with the mode Off, and so does a change of mode.
    qint64 offCost = 0;
    {
        Rig off;
        NEED_PARTS(off);
        usualTags(off);
        off.play->queue()->playNow({ off.a, off.c }, 0);
        const qint64 before = off.library->tracksRead();
        off.play->player()->next();
        offCost = off.library->tracksRead() - before;
    }
    Rig r;
    NEED_PARTS(r);
    usualTags(r);
    choose(r, u"controls.gain.track");
    r.play->queue()->playNow({ r.a, r.c }, 0);
    qint64 before = r.library->tracksRead();
    for (int i = 0; i < 40; ++i)
        r.play->player()->setVolume(0.2 + i * 0.01);
    r.play->player()->setMuted(true);
    r.play->player()->setMuted(false);
    QCOMPARE(r.library->tracksRead() - before, 0);
    QCOMPARE(vol(r), QString::number(0.59 * 0.5, 'f', 3));
    r.play->player()->next();
    QVERIFY2(r.library->tracksRead() - before <= offCost + 1, qPrintable(QString::number(r.library->tracksRead() - before)));
    before = r.library->tracksRead();
    choose(r, u"controls.gain.album");
    QVERIFY2(r.library->tracksRead() - before <= 1, qPrintable(QString::number(r.library->tracksRead() - before)));
    // The tags of the playing track changed and the library read it again: the next time the volume is set,
    // the new value counts.
    r.play->player()->setVolume(1.0);
    QCOMPARE(vol(r), s(u"0.250")); // c, by its album gain
    gain(r, r.c, "", "", "-6.0206 dB", "0.5");
    r.play->player()->setVolume(0.8);
    QCOMPARE(vol(r), s(u"0.400"));
}

QTEST_MAIN(PlaybackShellGainTest)
#include "tst_ui_playbackshell_gain.moc"
