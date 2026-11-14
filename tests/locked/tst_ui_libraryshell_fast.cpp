// LOCKED ACCEPTANCE TEST (the node is not read needlessly). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/library-shell.md (SH-x). Runs on the
// offscreen platform with a real library (in memory), a real playlist folder
// (in a temporary directory) and the real window parts.
#include <QFile>
#include <QAction>
#include <QLineEdit>
#include <QSettings>
#include <QLabel>
#include <QPointer>
#include <QSignalSpy>
#include <QSplitter>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest>

#include "actions/actionregistry.h"
#include "mainwindow/mainwindow.h"
#include "navigator/navigatorpanel.h"
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

QString fixture(const char *name)
{
    return QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/") + QLatin1String(name);
}

// Stops a test, as a failure, when the shell has not built its parts.
#define NEED_PARTS(r) \
    do { \
        if (!(r).shell->navigator() || !(r).shell->model() || !(r).shell->trackList()) \
            QFAIL("the shell has not built its navigator, model and track list"); \
    } while (false)

QStringList sorted(QStringList list)
{
    list.sort();
    return list;
}

struct Rig {
    QTemporaryDir dir;
    QString a, b, c, d, junk;
    std::unique_ptr<Library> library;
    PlaylistStore playlists;
    QSettings settings;
    ActionRegistry actions;
    MainWindow window;
    QStringList folders; // what the folder chooser will answer
    QStringList files; // what the file chooser will answer
    int asked = 0;
    QDateTime now = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);
    std::unique_ptr<LibraryShell> shell;

    Rig()
        : library(Library::open(QStringLiteral(":memory:")))
        , playlists(made(QStringLiteral("playlists")))
        , settings(dir.path() + QStringLiteral("/s.ini"), QSettings::IniFormat)
        , window(&actions, &settings)
    {
        made(QStringLiteral("music"));
        made(QStringLiteral("inbox"));
        a = put("basic.flac", "music/a.flac");
        b = put("basic.mp3", "music/b.mp3");
        c = put("basic.ogg", "music/c.ogg");
        junk = put("notaudio.mp3", "music/junk.mp3");
        d = put("basic.flac", "inbox/d.flac");
    }
    QString made(const QString &name)
    {
        QDir(dir.path()).mkpath(name);
        return dir.path() + QLatin1Char('/') + name;
    }
    QString put(const char *from, const char *to)
    {
        const QString target = dir.path() + QLatin1Char('/') + QLatin1String(to);
        if (!QFile::copy(fixture(from), target))
            QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
        return target;
    }
    void fill() // three in Music, one in the Inbox
    {
        for (const QString &p : { a, b, c })
            QVERIFY(library->addFile(p, QStringLiteral("Music"), now) != 0);
        QVERIFY(library->addFile(d, QStringLiteral("Inbox"), now) != 0);
    }
    void start()
    {
        ShellHooks hooks;
        hooks.chooseFolders = [this] {
            ++asked;
            return folders;
        };
        hooks.chooseFiles = [this] {
            ++asked;
            return files;
        };
        hooks.now = [this] { return now; };
        shell = std::make_unique<LibraryShell>(&window, &actions, library.get(), &playlists, &settings, hooks);
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
    QStringList shown() const { return sorted(shell->shownPaths()); }
    QStringList navigatorRows() const
    {
        QStringList out;
        auto *tree = window.findChild<QTreeWidget *>(s(u"navigatorTree"));
        if (!tree)
            return { s(u"NO TREE") };
        for (QTreeWidgetItemIterator it(tree); *it; ++it)
            out.append((*it)->text(0));
        return out;
    }
};

QList<qint64> idsOf(const QList<LibraryTrack> &tracks)
{
    QList<qint64> out;
    for (const LibraryTrack &t : tracks)
        out.append(t.id);
    std::sort(out.begin(), out.end());
    return out;
}

}

class LibraryShellFastTest : public QObject {
    Q_OBJECT
private slots:
    void narrowingReadsNothing();
    void theNodesTracksFollowTheLibrary();
};

void LibraryShellFastTest::narrowingReadsNothing()
{
    // SH-EXTEND: the node is read when it may have changed, and not otherwise.
    // A node source is asked once for every read of the node, so it counts them.
    (void)&nextRun; // a helper of the rig this file shares with the plan's own test
    Rig r;
    r.fill();
    r.start();
    NEED_PARTS(r);
    int reads = 0;
    r.shell->addNodeSource([&reads](const QString &, QList<LibraryTrack> *) {
        ++reads;
        return false;
    });
    QVERIFY(reads >= 1); // adding a source reads the node
    QSignalSpy changed(r.shell.get(), &LibraryShell::nodeTracksChanged);
    reads = 0;

    // Asking for the node's tracks reads nothing.
    for (int i = 0; i < 3; ++i)
        QCOMPARE(r.shell->nodeTracks().size(), 3);
    QCOMPARE(reads, 0);

    // Narrowing reads nothing and does not say the node changed: it did not.
    const QString flac = s(u"flac");
    const QString ogg = s(u"ogg");
    r.shell->setNarrowing([flac](const LibraryTrack &t) { return t.tags.format == flac; });
    QCOMPARE(r.shown(), QStringList({ r.a }));
    r.shell->setNarrowing([ogg](const LibraryTrack &t) { return t.tags.format == ogg; });
    QCOMPARE(r.shown(), QStringList({ r.c }));
    QCOMPARE(r.shell->nodeTracks().size(), 3);
    r.shell->setNarrowing({ });
    QCOMPARE(r.shown(), sorted({ r.a, r.b, r.c }));
    QCOMPARE(reads, 0);
    QCOMPARE(changed.count(), 0);
    // Neither does the search box.
    r.shell->trackList()->setSearchText(s(u"no such thing anywhere"));
    QCOMPARE(r.shown(), QStringList());
    r.shell->trackList()->setSearchText(QString());
    QCOMPARE(reads, 0);
    QCOMPARE(changed.count(), 0);

    // What does change the node reads it once and says so once.
    r.shell->setNarrowing([flac](const LibraryTrack &t) { return t.tags.format == flac; });
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Inbox")));
    QCOMPARE(reads, 1);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(idsOf(r.shell->nodeTracks()), QList<qint64>({ r.idOf(r.d) }));
    QCOMPARE(r.shown(), QStringList({ r.d })); // the narrowing holds across nodes
    r.shell->reloadNode();
    QCOMPARE(reads, 2);
    QCOMPARE(changed.count(), 2);
    r.run(u"view.refresh");
    QVERIFY(reads >= 3);
    const int before = reads;
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Music")));
    QCOMPARE(r.shown(), QStringList({ r.a }));
    QCOMPARE(r.shell->nodeTracks().size(), 3);
    QCOMPARE(reads, before + 1);
}

void LibraryShellFastTest::theNodesTracksFollowTheLibrary()
{
    // SH-EXTEND: on a library section the kept tracks follow the library
    // from the tracks it names, and nodeTracks() is right at every step.
    Rig r;
    r.fill();
    r.start();
    NEED_PARTS(r);
    int reads = 0;
    r.shell->addNodeSource([&reads](const QString &, QList<LibraryTrack> *) {
        ++reads;
        return false;
    });
    QSignalSpy changed(r.shell.get(), &LibraryShell::nodeTracksChanged);
    const auto music = [&r] { return idsOf(r.library->tracks(s(u"Music"))); };
    const auto step = [&](const char *what, int size, int signalsAtMost = 1) {
        QVERIFY2(idsOf(r.shell->nodeTracks()) == music(), what);
        QVERIFY2(r.shell->nodeTracks().size() == size, what);
        QVERIFY2(changed.count() >= 1, what); // the answer changed, and it was said
        QVERIFY2(reads <= signalsAtMost, what); // the sources may be asked whose node it is, once for each signal of the library
        changed.clear();
        reads = 0;
    };
    reads = 0;
    const QString e = r.put("basic.m4a", "music/e.m4a");
    QVERIFY(r.library->addFile(e, s(u"Music"), r.now) != 0);
    step("a track arrives", 4);
    QCOMPARE(r.library->setCategory({ r.idOf(r.a) }, s(u"Inbox")), 1);
    step("a track leaves for another section", 3);
    QCOMPARE(r.library->setCategory({ r.idOf(r.d), r.idOf(r.a) }, s(u"Music")), 2);
    step("two arrive from another section", 5, 2);
    for (const LibraryTrack &t : r.shell->nodeTracks())
        QCOMPARE(t.category, s(u"Music")); // as they are now, not as they were read
    QVERIFY(r.library->remove({ r.idOf(r.b) }) == 1);
    step("a track is removed", 4);
    // With a narrowing on: the list is narrowed, the node's tracks are all of them.
    const QString flac = s(u"flac");
    r.shell->setNarrowing([flac](const LibraryTrack &t) { return t.tags.format == flac; });
    const QString f = r.put("basic.flac", "music/f.flac");
    QVERIFY(r.library->addFile(f, s(u"Music"), r.now) != 0);
    step("a track arrives under a narrowing", 5);
    QCOMPARE(r.shown(), sorted({ r.a, r.d, f }));
    r.shell->setNarrowing({ });
    QCOMPARE(r.shown(), sorted({ r.a, r.c, r.d, e, f }));
    // A playlist node is read again when the playlist changes, as before.
    const QString mix = r.playlists.create(s(u"Mix"));
    QVERIFY(r.playlists.setTracks(mix, { r.c }));
    QVERIFY(r.shell->navigator()->selectNode(s(u"playlist:") + mix));
    QCOMPARE(idsOf(r.shell->nodeTracks()), QList<qint64>({ r.idOf(r.c) }));
    QCOMPARE(r.playlists.append(mix, { r.a }), 1);
    QCOMPARE(r.shell->nodeTracks().size(), 2);
    // and a library change on it reads it again (SH-LIVE).
    QVERIFY(r.library->remove({ r.idOf(r.a) }) == 1);
    QCOMPARE(idsOf(r.shell->nodeTracks()), QList<qint64>({ r.idOf(r.c) }));
}

QTEST_MAIN(LibraryShellFastTest)
#include "tst_ui_libraryshell_fast.moc"
