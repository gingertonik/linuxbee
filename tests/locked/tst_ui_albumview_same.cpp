// LOCKED ACCEPTANCE TEST (the album view: the same tracks fill nothing). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/album-view.md (AV-x). Runs on the
// offscreen platform. The last tests use a real library (in memory), the
// real window and the real library shell.
#include <QBoxLayout>
#include <QHeaderView>
#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>
#include <QtTest>

#include "actions/actionregistry.h"
#include "albumview/albumtracksview.h"
#include "library/albumgroups.h"
#include "library/artistindex.h"
#include "mainwindow/mainwindow.h"
#include "navigator/navigatorpanel.h"
#include "shell/albumviewing.h"
#include "shell/libraryshell.h"
#include "tracklist/tracklistmodel.h"
#include "tracklist/tracklistpage.h"
#include "tracklist/tracklistpage.h"

using namespace linuxbee;
using namespace linuxbee::ui;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

// A settings file as the next run of the program reads it (every value text).
QString nextRun(QSettings &settings)
{
    static int count = 0;
    settings.sync();
    const QString copy = settings.fileName() + QStringLiteral(".run%1.ini").arg(++count);
    QFile::remove(copy);
    QFile::copy(settings.fileName(), copy);
    return copy;
}

struct Spec {
    qint64 id;
    const char16_t *albumArtist;
    const char16_t *artist;
    const char16_t *album;
    const char16_t *year;
    const char16_t *disc;
    const char16_t *number;
    const char16_t *title;
    int ms;
    const char16_t *genre;
};

LibraryTrack made(const Spec &spec)
{
    LibraryTrack t;
    t.id = spec.id;
    t.path = QStringLiteral("/music/%1.flac").arg(spec.id, 2, 10, QLatin1Char('0'));
    const auto put = [&t](const char16_t *field, const char16_t *value) {
        if (*value)
            t.tags.fields.insert(s(field), { s(value) });
    };
    put(u"Album Artist", spec.albumArtist);
    put(u"Artist", spec.artist);
    put(u"Album", spec.album);
    put(u"Year", spec.year);
    put(u"Disc#", spec.disc);
    put(u"Track#", spec.number);
    put(u"Title", spec.title);
    put(u"Genre", spec.genre);
    t.tags.audio.durationMs = spec.ms;
    t.tags.format = QStringLiteral("flac"); // a track read from a file: it has a Time
    return t;
}

// Nine tracks, deliberately out of order. In the order of the view their ids
// read 7, 9, 4, 6, 5, 1, 2, 3, 8, in six albums.
QList<LibraryTrack> nine()
{
    return {
        made({ 3, u"Boards of Canada", u"Boards of Canada", u"Geogaddi", u"2002", u"", u"1", u"Ready Lets Go", 59000, u"IDM" }),
        made({ 8, u"", u"", u"", u"", u"", u"", u"Loose", 0, u"" }),
        made({ 5, u"The Beatles", u"The Beatles", u"Abbey Road", u"1969", u"2", u"1", u"Her Majesty", 23000, u"" }),
        made({ 2, u"Boards of Canada", u"Boards of Canada", u"Music Has the Right to Children", u"1998", u"", u"2",
            u"An Eagle in Your Mind", 383000, u"Electronic" }),
        made({ 9, u"", u"Adele", u"", u"", u"", u"", u"", 0, u"" }),
        made({ 6, u"The Beatles", u"George Harrison", u"Abbey Road", u"1969", u"1", u"2", u"Something", 182000, u"Rock" }),
        made({ 1, u"Boards of Canada", u"Boards of Canada", u"Music Has the Right to Children", u"1998-04-20", u"", u"1",
            u"Wildlife Analysis", 77000, u"" }),
        made({ 7, u"", u"Adele", u"21", u"2011", u"1", u"1", u"Rolling in the Deep", 228000, u"Pop" }),
        made({ 4, u"The Beatles", u"The Beatles", u"Abbey Road", u"1969", u"1", u"1", u"Come Together", 259000, u"" }),
    };
}

QList<qint64> idsOf(const QList<LibraryTrack> &tracks)
{
    QList<qint64> out;
    for (const LibraryTrack &t : tracks)
        out.append(t.id);
    return out;
}

AlbumGroup named(const char16_t *artist)
{
    AlbumGroup g;
    g.artist = s(artist);
    g.album = QStringLiteral("x");
    g.trackIds = { 1 };
    return g;
}

QTreeWidget *treeOf(AlbumTracksView &v)
{
    return v.findChild<QTreeWidget *>(s(u"albumTree"));
}

// The row of a track, or of the heading of a group when `id` is below 0 (-1 the first).
QTreeWidgetItem *row(AlbumTracksView &v, qint64 id)
{
    QTreeWidget *tree = treeOf(v);
    if (!tree)
        return nullptr;
    if (id < 0)
        return tree->topLevelItem(int(-id - 1));
    const QList<AlbumGroup> groups = v.groups();
    for (int g = 0; g < groups.size() && g < tree->topLevelItemCount(); ++g) {
        QTreeWidgetItem *head = tree->topLevelItem(g);
        int at = 0;
        for (int i = 0; i < head->childCount(); ++i) {
            if (!(head->child(i)->flags() & Qt::ItemIsSelectable))
                continue; // a disc row
            if (groups.at(g).trackIds.value(at) == id)
                return head->child(i);
            ++at;
        }
    }
    return nullptr;
}

struct Rig {
    QTemporaryDir dir;
    QString mp3, flac, m4a, ogg, inboxFlac;
    std::unique_ptr<Library> library;
    PlaylistStore playlists;
    QSettings settings;
    ActionRegistry actions;
    MainWindow window;
    QDateTime now = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);
    std::unique_ptr<LibraryShell> shell;

    Rig()
        : library(Library::open(QStringLiteral(":memory:")))
        , playlists(made(QStringLiteral("playlists")))
        , settings(dir.path() + QStringLiteral("/s.ini"), QSettings::IniFormat)
        , window(&actions, &settings)
    {
        made(QStringLiteral("music"));
        mp3 = put("basic.mp3", "music/a.mp3"); // Album Artist: Adele
        flac = put("basic.flac", "music/b.flac"); // Album Artist: Various Artists
        m4a = put("basic.m4a", "music/c.m4a"); // Album Artist: M4A Album Artist
        ogg = put("basic.ogg", "music/d.ogg"); // no Album Artist
        inboxFlac = put("basic.flac", "music/e.flac");
        for (const QString &p : { mp3, flac, m4a, ogg })
            if (library->addFile(p, QStringLiteral("Music"), now) == 0)
                QTest::qFail("a fixture did not go into the library", __FILE__, __LINE__);
        if (library->addFile(inboxFlac, QStringLiteral("Inbox"), now) == 0)
            QTest::qFail("a fixture did not go into the library", __FILE__, __LINE__);
        shell = std::make_unique<LibraryShell>(&window, &actions, library.get(), &playlists, &settings, ShellHooks { });
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
    QStringList shown() const
    {
        QStringList list = shell->shownPaths();
        list.sort();
        return list;
    }
};


#define NEED_TREE(v) \
    do { \
        if (!treeOf(v)) \
            QFAIL("the album view has not built its tree"); \
    } while (false)

#define NEED_VIEW(viewing) \
    do { \
        if (!(viewing).view() || !treeOf(*(viewing).view())) \
            QFAIL("the album view is not in the shell"); \
    } while (false)

QList<qint64> ordered(QList<qint64> ids)
{
    std::sort(ids.begin(), ids.end());
    return ids;
}

}

class AlbumViewSameTest : public QObject {
    Q_OBJECT
private slots:
    void headingsSpanTheView();
    void theSameTracksFillNothing();
};

void AlbumViewSameTest::headingsSpanTheView()
{
    // AV-VIEW: every heading spans the columns, however the spans are set.
    (void)&nextRun;
    (void)&named;
    (void)&row;
    (void)&ordered;
    AlbumTracksView v;
    NEED_TREE(v);
    v.setTracks(nine());
    QTreeWidget *tree = treeOf(v);
    QVERIFY(tree->topLevelItemCount() >= 2);
    QCOMPARE(tree->topLevelItemCount(), int(v.groups().size()));
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QVERIFY2(tree->isFirstColumnSpanned(i, QModelIndex()), qPrintable(QString::number(i)));
        QVERIFY(tree->topLevelItem(i)->isExpanded());
    }
    // And again after another fill.
    v.setTracks(nine().mid(0, 4));
    QVERIFY(tree->topLevelItemCount() >= 1);
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        QVERIFY2(tree->isFirstColumnSpanned(i, QModelIndex()), qPrintable(QString::number(i)));
    QVERIFY(!idsOf(nine()).isEmpty());
}

void AlbumViewSameTest::theSameTracksFillNothing()
{
    // AV-FILL: a change of the list that leaves the same tracks in the same order fills nothing.
    Rig r;
    if (!r.shell->navigator())
        QFAIL("plan 30's library shell is not there");
    AlbumViewing viewing(r.shell.get(), &r.settings);
    NEED_VIEW(viewing);
    QCoreApplication::processEvents();
    const auto listed = [&r] {
        QList<qint64> ids;
        for (int i = 0; i < r.shell->model()->rowCount(); ++i)
            ids.append(r.shell->model()->trackId(i));
        return ids;
    };
    const auto shown = [&viewing] {
        QList<qint64> ids;
        for (const AlbumGroup &g : viewing.view()->groups())
            ids += g.trackIds;
        return ids;
    };
    QCOMPARE(listed().size(), 4);

    // Narrowed by a search to some of them.
    r.shell->trackList()->setSearchText(s(u"adel"));
    const QList<qint64> some = listed();
    QVERIFY2(!some.isEmpty() && some.size() < 4, "the search text of this test does not narrow the fixtures");
    int fills = viewing.view()->fills(); // asked for: up to date
    QCOMPARE(shown().size(), some.size());

    // One more letter, the same tracks: nothing is filled, now or at the next turn.
    r.shell->trackList()->setSearchText(s(u"adele"));
    QCOMPARE(listed(), some);
    QCOMPARE(viewing.view()->fills(), fills);
    QCoreApplication::processEvents();
    QCOMPARE(viewing.view()->fills(), fills);
    QCOMPARE(shown().size(), some.size());

    // Other tracks: filled.
    r.shell->trackList()->setSearchText(s(u"zzz"));
    QCOMPARE(viewing.view()->fills(), fills + 1);
    QCOMPARE(shown().size(), 0);
    r.shell->trackList()->setSearchText(s(u"adele"));
    QCOMPARE(viewing.view()->fills(), fills + 2);
    QCOMPARE(shown().size(), some.size());

    // The same tracks, but one of them changed in the library: filled, once.
    fills = viewing.view()->fills();
    QVERIFY(r.library->recordPlay(some.first(), r.now.addSecs(60)));
    QCOMPARE(listed(), some);
    QTRY_COMPARE(viewing.view()->fills(), fills + 1);
    QCoreApplication::processEvents();
    QCOMPARE(viewing.view()->fills(), fills + 1);

    // The node chosen again: filled, though it holds the same tracks.
    fills = viewing.view()->fills();
    r.shell->trackList()->setSearchText(QString());
    QCOMPARE(viewing.view()->fills(), fills + 1);
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Inbox")));
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Music")));
    QCOMPARE(viewing.view()->fills(), fills + 2);
}

QTEST_MAIN(AlbumViewSameTest)
#include "tst_ui_albumview_same.moc"
