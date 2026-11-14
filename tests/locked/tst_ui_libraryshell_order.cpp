// LOCKED ACCEPTANCE TEST (the order of a playlist). Written by the reviewer, not the builder.
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

class LibraryShellOrderTest : public QObject {
    Q_OBJECT
private slots:
    void theModelInTheOrderGiven();
    void aPlaylistInItsOwnOrder();
    void playingTracksInQueueOrder();
    void aHeaderClickSorts();
};

void LibraryShellOrderTest::theModelInTheOrderGiven()
{
    // T-GIVEN
    TrackListModel model;
    QVERIFY(!model.givenOrder());
    model.setSortFields({ s(u"Title") });
    const QList<LibraryTrack> given = { crafted(3, u"Cherry"), crafted(1, u"Apple pie"), crafted(2, u"Banana"), crafted(5, u"Apple tart") };
    model.setTracks(given);
    QCOMPARE(rows(model), QList<qint64>({ 1, 5, 2, 3 })); // sorted, as before
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    model.setGivenOrder(true);
    QVERIFY(model.givenOrder());
    QCOMPARE(reset.count(), 1);
    model.setGivenOrder(true); // no change: nothing
    QCOMPARE(reset.count(), 1);
    QCOMPARE(rows(model), QList<qint64>({ 3, 1, 2, 5 })); // the order of setTracks
    QCOMPARE(model.rowOf(2), 2);
    QCOMPARE(model.sortFields(), QStringList({ s(u"Title") })); // kept for when it sorts again
    // A track that arrives goes to the end; one that is replaced keeps its place.
    model.upsertTracks({ crafted(4, u"Aardvark"), crafted(1, u"Zebra") });
    QCOMPARE(rows(model), QList<qint64>({ 3, 1, 2, 5, 4 }));
    QCOMPARE(model.data(model.index(1, model.columns().indexOf(s(u"Title")))).toString(), s(u"Zebra"));
    model.removeTracks({ 2 });
    QCOMPARE(rows(model), QList<qint64>({ 3, 1, 5, 4 }));
    // The search narrows and the order stays.
    model.setFilter(SearchQuery::parse(s(u"ar")));
    QCOMPARE(rows(model), QList<qint64>({ 5, 4 })); // Apple tart, Aardvark
    model.setFilter(SearchQuery::parse(QString()));
    QCOMPARE(rows(model), QList<qint64>({ 3, 1, 5, 4 }));
    // Setting the sort keys while in the given order changes no row.
    model.setSortFields({ s(u"-Title") });
    QCOMPARE(rows(model), QList<qint64>({ 3, 1, 5, 4 }));
    // Off again: sorted by the keys it has. On again: the order given, still known.
    model.setGivenOrder(false);
    QCOMPARE(rows(model), QList<qint64>({ 1, 3, 5, 4 })); // Zebra, Cherry, Apple tart, Aardvark
    model.setGivenOrder(true);
    QCOMPARE(rows(model), QList<qint64>({ 3, 1, 5, 4 }));
    // setTracks gives a new order.
    model.setTracks({ given.at(2), given.at(0) });
    QCOMPARE(rows(model), QList<qint64>({ 2, 3 }));
    // It is not stored: a list starts sorted.
    QTemporaryDir dir;
    QSettings settings(dir.path() + s(u"/m.ini"), QSettings::IniFormat);
    model.save(settings, s(u"list"));
    settings.beginGroup(s(u"list"));
    QCOMPARE(sorted(settings.childKeys()), QStringList({ s(u"columns"), s(u"sort") }));
    settings.endGroup();
    QSettings later(nextRun(settings), QSettings::IniFormat);
    TrackListModel other;
    other.load(later, s(u"list"));
    QVERIFY(!other.givenOrder());
    model.load(later, s(u"list")); // and loading does not switch it off or on
    QVERIFY(model.givenOrder());
}

void LibraryShellOrderTest::aPlaylistInItsOwnOrder()
{
    // SH-NODE: a playlist is shown in the playlist's order.
    Rig r;
    r.fill();
    const QString mix = r.playlists.create(s(u"Mix"));
    const QString back = r.playlists.create(s(u"Back"));
    QVERIFY(r.playlists.setTracks(mix, { r.c, r.a, r.dir.path() + s(u"/elsewhere.flac"), r.d }));
    QVERIFY(r.playlists.setTracks(back, { r.d, r.a, r.c }));
    r.start();
    NEED_PARTS(r);
    QCOMPARE(r.shell->currentNode(), s(u"library:Music"));
    QVERIFY(!r.shell->model()->givenOrder());
    const QStringList music = r.shell->shownPaths();
    const QStringList sortBefore = r.shell->model()->sortFields();
    QCOMPARE(music.size(), 3);

    QVERIFY(r.shell->navigator()->selectNode(s(u"playlist:") + mix));
    QVERIFY(r.shell->model()->givenOrder());
    QCOMPARE(r.shell->shownPaths(), QStringList({ r.c, r.a, r.d }));
    QVERIFY(r.shell->navigator()->selectNode(s(u"playlist:") + back));
    QCOMPARE(r.shell->shownPaths(), QStringList({ r.d, r.a, r.c })); // the reverse: no sorting gives both
    QVERIFY(r.shell->navigator()->selectNode(s(u"playlist:") + mix));
    // Changed while on show: read again, in its order.
    QCOMPARE(r.playlists.append(mix, { r.b }), 1);
    QCOMPARE(r.shell->shownPaths(), QStringList({ r.c, r.a, r.d, r.b }));
    QVERIFY(r.playlists.setTracks(mix, { r.b, r.d, r.c, r.a }));
    QCOMPARE(r.shell->shownPaths(), QStringList({ r.b, r.d, r.c, r.a }));
    // The library changes a track of it: still in its order (SH-LIVE reads the node again).
    r.run(u"view.refresh");
    QCOMPARE(r.shell->shownPaths(), QStringList({ r.b, r.d, r.c, r.a }));
    // What is played from here is what is shown (SH-PLAY).
    QSignalSpy play(r.shell.get(), &LibraryShell::playRequested);
    emit r.shell->trackList()->trackActivated(r.idOf(r.c));
    QCOMPARE(play.count(), 1);
    QCOMPARE(play.at(0).at(0).toStringList(), QStringList({ r.b, r.d, r.c, r.a }));
    QCOMPARE(play.at(0).at(1).toInt(), 2);

    // A library section is sorted as it was, and the list's sorting was never touched.
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Music")));
    QVERIFY(!r.shell->model()->givenOrder());
    QCOMPARE(r.shell->shownPaths(), music);
    QCOMPARE(r.shell->model()->sortFields(), sortBefore);
    QVERIFY(r.shell->navigator()->selectNode(s(u"playlist:") + mix));
    r.shell->saveState();
    QSettings later(nextRun(r.settings), QSettings::IniFormat);
    QCOMPARE(later.value(s(u"tracklist/sort")).toStringList(), sortBefore);
    // A later start on the playlist node shows it in its order at once.
    LibraryShell again(&r.window, &r.actions, r.library.get(), &r.playlists, &later, { });
    QCOMPARE(again.currentNode(), s(u"playlist:") + mix);
    QCOMPARE(again.shownPaths(), QStringList({ r.b, r.d, r.c, r.a }));
}

void LibraryShellOrderTest::playingTracksInQueueOrder()
{
    // SH-NODE: Playing Tracks is shown in the order it was told.
    Rig r;
    r.fill();
    r.start();
    NEED_PARTS(r);
    r.shell->setPlayingPaths({ r.c, r.d, r.dir.path() + s(u"/elsewhere.flac"), r.a });
    QVERIFY(r.shell->navigator()->selectNode(s(u"playing")));
    QVERIFY(r.shell->model()->givenOrder());
    QCOMPARE(r.shell->shownPaths(), QStringList({ r.c, r.d, r.a }));
    r.shell->setPlayingPaths({ r.a, r.b, r.d, r.c }); // told while on show
    QCOMPARE(r.shell->shownPaths(), QStringList({ r.a, r.b, r.d, r.c }));
    r.shell->setPlayingPaths({ r.d, r.c, r.b, r.a });
    QCOMPARE(r.shell->shownPaths(), QStringList({ r.d, r.c, r.b, r.a }));
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Inbox")));
    QVERIFY(!r.shell->model()->givenOrder());
}

void LibraryShellOrderTest::aHeaderClickSorts()
{
    // T-GIVEN: a click on a header sorts, here as anywhere; choosing the node again shows its order.
    Rig r;
    r.fill();
    const QString mix = r.playlists.create(s(u"Mix"));
    const QString back = r.playlists.create(s(u"Back"));
    QVERIFY(r.playlists.setTracks(mix, { r.c, r.a, r.d, r.b }));
    QVERIFY(r.playlists.setTracks(back, { r.b, r.d, r.a, r.c }));
    r.start();
    NEED_PARTS(r);
    TrackListModel *model = r.shell->model();
    const int title = int(model->columns().indexOf(s(u"Title")));
    QVERIFY(title >= 0);
    QVERIFY(r.shell->navigator()->selectNode(s(u"playlist:") + mix));
    model->sort(title, Qt::AscendingOrder);
    QVERIFY(!model->givenOrder());
    QCOMPARE(model->sortFields().value(0), s(u"Title"));
    const QStringList byTitle = r.shell->shownPaths();
    QCOMPARE(sorted(byTitle), sorted({ r.a, r.b, r.c, r.d }));
    QVERIFY(r.shell->navigator()->selectNode(s(u"playlist:") + back));
    QVERIFY(model->givenOrder());
    QCOMPARE(r.shell->shownPaths(), QStringList({ r.b, r.d, r.a, r.c }));
    model->sort(title, Qt::AscendingOrder); // the same key again: it still sorts
    QVERIFY(!model->givenOrder());
    QCOMPARE(r.shell->shownPaths(), byTitle);
    QVERIFY(r.shell->navigator()->selectNode(s(u"playlist:") + mix));
    QCOMPARE(r.shell->shownPaths(), QStringList({ r.c, r.a, r.d, r.b }));
}

QTEST_MAIN(LibraryShellOrderTest)
#include "tst_ui_libraryshell_order.moc"
