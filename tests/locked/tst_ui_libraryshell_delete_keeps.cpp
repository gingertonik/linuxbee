// LOCKED ACCEPTANCE TEST (the library shell: Delete Files keeps the track's row for thirty days).
// Written by the reviewer, not the builder. The builder makes these pass by changing src/, never
// by editing this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Rules: docs/spec/library-shell.md (SH-DELETE, step 3) and docs/spec/library.md (L-DELETED).
// The owner's ruling: a file taken back out of the trash is the track it was, with its plays
// and its date added. The rig is the one of tst_ui_libraryshell_delete.cpp: the real window,
// library and shell on the offscreen platform, copies of fixtures in a temporary folder, and
// a trash of this test's own (XDG_DATA_HOME). No real file and no real trash is touched.
#include <QAction>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

#include "actions/actionregistry.h"
#include "library/library.h"
#include "mainwindow/mainwindow.h"
#include "playlists/playliststore.h"
#include "shell/libraryshell.h"
#include "tracklist/tracklistmodel.h"
#include "tracklist/tracklistpage.h"

using namespace linuxbee;
using namespace linuxbee::ui;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

struct Rig {
    QTemporaryDir dir;
    QString a, b, c, d;
    std::unique_ptr<Library> library;
    PlaylistStore playlists;
    QSettings settings;
    ActionRegistry actions;
    MainWindow window;
    QDateTime now = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);
    std::unique_ptr<LibraryShell> shell;
    bool yes = true;
    QStringList asked;
    int existedWhenAsked = 0; // how many of the four files were there at the question

    Rig()
        : library(Library::open(QStringLiteral(":memory:")))
        , playlists(made(QStringLiteral("playlists")))
        , settings(dir.path() + QStringLiteral("/s.ini"), QSettings::IniFormat)
        , window(&actions, &settings)
    {
        qputenv("XDG_DATA_HOME", QFile::encodeName(dir.path() + QStringLiteral("/xdg-data")));
        made(QStringLiteral("music/album"));
        a = put("basic.flac", "music/album/a.flac");
        b = put("basic.mp3", "music/album/b.mp3");
        c = put("basic.ogg", "music/album/c.ogg");
        d = put("basic.m4a", "music/d.m4a");
        for (const QString &p : { a, b, c, d })
            if (library->addFile(p, QStringLiteral("Music"), now) == 0)
                QTest::qFail("a fixture did not go into the library", __FILE__, __LINE__);
        ShellHooks hooks;
        hooks.now = [this] { return now; };
        hooks.confirm = [this](const QString &question) {
            asked.append(question);
            existedWhenAsked = 0;
            for (const QString &p : { a, b, c, d })
                existedWhenAsked += QFileInfo::exists(p) ? 1 : 0;
            return yes;
        };
        shell = std::make_unique<LibraryShell>(
            &window, &actions, library.get(), &playlists, &settings, hooks);
    }
    QString made(const QString &name)
    {
        QDir(dir.path()).mkpath(name);
        return dir.path() + QLatin1Char('/') + name;
    }
    QString put(const char *from, const char *to)
    {
        const QString target = dir.path() + QLatin1Char('/') + QLatin1String(to);
        if (!QFile::copy(
                QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/") + QLatin1String(from),
                target))
            QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
        return target;
    }
    QString trashed(const char *name) const
    {
        return dir.path() + QStringLiteral("/xdg-data/Trash/files/") + QLatin1String(name);
    }
    qint64 id(const QString &path) const
    {
        const auto t = library->trackByPath(path);
        return t ? t->id : 0;
    }
    void select(const QStringList &paths)
    {
        QList<qint64> ids;
        for (const QString &p : paths)
            ids.append(id(p));
        shell->trackList()->selectTracks(ids);
    }
    void run()
    {
        if (QAction *action = actions.action(QStringLiteral("file.delete")))
            action->trigger();
        else
            QTest::qFail("no such action", __FILE__, __LINE__);
    }
};

#define NEED_DELETE(r)                                                                             \
    do {                                                                                           \
        if (!(r).shell->trackList() || !(r).actions.isImplemented(QStringLiteral("file.delete")))  \
            QFAIL("Delete Files has not been built");                                              \
    } while (false)

const qint64 kDay = 24 * 60 * 60;

}

class LibraryShellDeleteKeepsTest : public QObject {
    Q_OBJECT
private slots:
    void aFileTakenBackIsTheTrackItWas();
    void deletedAtTheShellsTime();
    void whatDidNotGoIsNotDeleted();
};

void LibraryShellDeleteKeepsTest::aFileTakenBackIsTheTrackItWas()
{
    // SH-DELETE step 3, L-DELETED: the track leaves the list at once; its file put back where
    // it was, a scan brings the same track back: its id, its plays, its date added.
    Rig r;
    NEED_DELETE(r);
    const qint64 id = r.id(r.a);
    QVERIFY(r.library->recordPlay(id, r.now.addSecs(60)));
    QVERIFY(r.library->recordPlay(id, r.now.addSecs(120)));
    r.select({ r.a, r.b });
    QSignalSpy removed(r.library.get(), &Library::tracksRemoved);
    r.run();
    QCOMPARE(removed.count(), 1);
    QCOMPARE(r.library->count(), 2);
    QCOMPARE(r.shell->model()->rowCount(), 2);
    QVERIFY(!r.library->trackByPath(r.a).has_value());
    QCOMPARE(r.shell->lastMessage(), s(u"Moved 2 files to the trash"));

    QVERIFY(QFile::rename(r.trashed("a.flac"), r.a)); // taken back out of the trash
    ScanOptions options;
    options.now = r.now.addSecs(3 * kDay);
    const ScanReport report = r.library->scan({ r.dir.path() + s(u"/music") }, options);
    QCOMPARE(report.found, QList<qint64>({ id }));
    QCOMPARE(report.added, QList<qint64>());
    const LibraryTrack back = r.library->trackByPath(r.a).value_or(LibraryTrack());
    QCOMPARE(back.id, id);
    QCOMPARE(back.playCount, 2);
    QCOMPARE(back.dateAdded, r.now);
    QVERIFY(!back.missing);
    QCOMPARE(r.library->count(), 3);
    QTRY_COMPARE(r.shell->model()->rowCount(), 3); // the list has it again
}

void LibraryShellDeleteKeepsTest::deletedAtTheShellsTime()
{
    // SH-DELETE step 3: the time of the deleting is the shell's `now`. So the row goes thirty
    // days after that, and not a second before.
    Rig r;
    NEED_DELETE(r);
    r.select({ r.a });
    r.run();
    QCOMPARE(r.library->count(), 3);
    QCOMPARE(r.library->expireDeleted(r.now.addSecs(30 * kDay - 1)), 0);
    QCOMPARE(r.library->expireDeleted(r.now.addSecs(30 * kDay)), 1);
    QCOMPARE(r.library->expireDeleted(r.now.addSecs(31 * kDay)), 0);
}

void LibraryShellDeleteKeepsTest::whatDidNotGoIsNotDeleted()
{
    // SH-DELETE step 3: a track whose file was not there stays a track; only the one whose
    // file went is deleted.
    Rig r;
    NEED_DELETE(r);
    QVERIFY(QFile::remove(r.b));
    r.select({ r.a, r.b });
    r.run();
    QCOMPARE(r.shell->lastMessage(), s(u"Moved 1 file to the trash; 1 was not there"));
    QCOMPARE(r.library->count(), 3);
    QVERIFY(r.library->trackByPath(r.b).has_value());
    QCOMPARE(r.library->expireDeleted(r.now.addSecs(400 * kDay)), 1); // a alone
    QCOMPARE(r.library->count(), 3);
}

QTEST_MAIN(LibraryShellDeleteKeepsTest)
#include "tst_ui_libraryshell_delete_keeps.moc"
