// LOCKED ACCEPTANCE TEST (a sort that holds; the kept tracks). Written by the reviewer, not the builder.
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

LibraryTrack crafted(qint64 id, const char16_t *title)
{
    LibraryTrack t;
    t.id = id;
    t.path = QStringLiteral("/music/%1.flac").arg(id);
    t.category = QStringLiteral("Music");
    t.tags.format = QStringLiteral("flac");
    t.tags.fields.insert(QStringLiteral("Title"), { s(title) });
    return t;
}

QList<qint64> rows(const TrackListModel &model)
{
    QList<qint64> out;
    for (int row = 0; row < model.rowCount(); ++row)
        out.append(model.trackId(row));
    return out;
}

}

class LibraryShellKeptTest : public QObject {
    Q_OBJECT
private slots:
    void aSortHoldsUntilTheNodeIsChosen();
    void aChangedTrackIsTheOneKept();
    void theNodeItStartsOn();
};

void LibraryShellKeptTest::aSortHoldsUntilTheNodeIsChosen()
{
    // SH-NODE: the shell switches the given order on when a playlist is
    // chosen and on view.refresh, and at no other time.
    // helpers of the rig this file shares with the test it grew from
    (void)&nextRun;
    (void)&crafted;
    (void)&rows;
    Rig r;
    r.fill();
    const QString mix = r.playlists.create(s(u"Mix"));
    QVERIFY(r.playlists.setTracks(mix, { r.c, r.a, r.b }));
    r.start();
    NEED_PARTS(r);
    TrackListModel *model = r.shell->model();
    const int title = int(model->columns().indexOf(s(u"Title")));
    QVERIFY(title >= 0);
    QVERIFY(r.shell->navigator()->selectNode(s(u"playlist:") + mix));
    QVERIFY(model->givenOrder());
    QCOMPARE(r.shell->shownPaths(), QStringList({ r.c, r.a, r.b }));

    model->sort(title, Qt::AscendingOrder);
    QVERIFY(!model->givenOrder());
    const QStringList byTitle = r.shell->shownPaths();
    QCOMPARE(sorted(byTitle), sorted({ r.a, r.b, r.c }));
    QVERIFY(byTitle != QStringList({ r.c, r.a, r.b })); // or the steps below would show nothing

    // None of these puts the playlist's order back.
    r.shell->setNarrowing([](const LibraryTrack &) { return true; });
    QVERIFY2(!model->givenOrder(), "a narrowing");
    QCOMPARE(r.shell->shownPaths(), byTitle);
    r.shell->setNarrowing({ });
    QVERIFY2(!model->givenOrder(), "the end of a narrowing");
    r.shell->trackList()->setSearchText(s(u"zzz"));
    r.shell->trackList()->setSearchText(QString());
    QVERIFY2(!model->givenOrder(), "the search box");
    QCOMPARE(r.shell->shownPaths(), byTitle);
    QVERIFY(r.library->recordPlay(r.idOf(r.a), r.now));
    QVERIFY2(!model->givenOrder(), "a change of the library");
    QCOMPARE(r.shell->shownPaths(), byTitle);
    r.shell->reloadNode();
    QVERIFY2(!model->givenOrder(), "the node read again");
    QCOMPARE(r.shell->shownPaths(), byTitle);
    QCOMPARE(r.playlists.append(mix, { r.d }), 1);
    QVERIFY2(!model->givenOrder(), "a change of the playlist");
    QCOMPARE(r.shell->shownPaths().size(), 4);
    {
        // the track that arrived fell into the sort: the other three keep their order
        QStringList others = r.shell->shownPaths();
        others.removeAll(r.d);
        QCOMPARE(others, byTitle);
    }

    // The user asks for the node afresh: its own order.
    r.run(u"view.refresh");
    QVERIFY(model->givenOrder());
    QCOMPARE(r.shell->shownPaths(), QStringList({ r.c, r.a, r.b, r.d }));
    model->sort(title, Qt::AscendingOrder);
    QVERIFY(!model->givenOrder());
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Music")));
    QVERIFY(!model->givenOrder());
    QVERIFY(r.shell->navigator()->selectNode(s(u"playlist:") + mix));
    QVERIFY(model->givenOrder());
    QCOMPARE(r.shell->shownPaths(), QStringList({ r.c, r.a, r.b, r.d }));
    // A library section is never in a given order, whatever happens on it.
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Music")));
    r.run(u"view.refresh");
    r.shell->reloadNode();
    QVERIFY(!model->givenOrder());
}

void LibraryShellKeptTest::aChangedTrackIsTheOneKept()
{
    // SH-EXTEND: on a library section a track the library names is replaced
    // in the kept tracks by the track as it is now.
    Rig r;
    r.fill();
    r.start();
    NEED_PARTS(r);
    const qint64 id = r.idOf(r.a);
    const auto kept = [&r, id]() -> LibraryTrack {
        for (const LibraryTrack &t : r.shell->nodeTracks())
            if (t.id == id)
                return t;
        return { };
    };
    QCOMPARE(kept().id, id);
    QCOMPARE(kept().playCount, 0);
    QVERIFY(r.library->recordPlay(id, r.now));
    QCOMPARE(kept().playCount, 1);
    QVERIFY(r.library->recordPlay(id, r.now.addSecs(600)));
    QCOMPARE(kept().playCount, 2);
    QCOMPARE(r.shell->nodeTracks().size(), 3);
    // Under a narrowing too: what is narrowed away is still kept as it is now.
    r.shell->setNarrowing([id](const LibraryTrack &t) { return t.id != id; });
    QVERIFY(r.library->recordPlay(id, r.now.addSecs(1200)));
    QCOMPARE(kept().playCount, 3);
    r.shell->setNarrowing({ });
}

void LibraryShellKeptTest::theNodeItStartsOn()
{
    // SH-NODE: the node the shell starts on counts as chosen.
    Rig r;
    r.fill();
    const QString mix = r.playlists.create(s(u"Mix"));
    QVERIFY(r.playlists.setTracks(mix, { r.c, r.a, r.b }));
    r.settings.setValue(s(u"shell/node"), s(u"playlist:") + mix);
    r.start();
    NEED_PARTS(r);
    QCOMPARE(r.shell->currentNode(), s(u"playlist:") + mix);
    QVERIFY(r.shell->model()->givenOrder());
    QCOMPARE(r.shell->shownPaths(), QStringList({ r.c, r.a, r.b }));
}

QTEST_MAIN(LibraryShellKeptTest)
#include "tst_ui_libraryshell_kept.moc"
