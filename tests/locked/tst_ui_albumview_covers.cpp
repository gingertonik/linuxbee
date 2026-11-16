// LOCKED ACCEPTANCE TEST (the album view: a cover at each heading). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/album-view.md (AV-COVER). The
// pictures are made here, in a temporary folder. Runs on the offscreen
// platform, with a real window and the real library shell.
#include <QBoxLayout>
#include <QHeaderView>
#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QScrollBar>
#include <QSemaphore>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>
#include <QtTest>

#include "actions/actionregistry.h"
#include "albumview/albumtracksview.h"
#include "covers/covercache.h"
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

const int kPatience = 5000;

// 60 albums of 5 tracks in folders of their own; album `a` is by "Artist a" (three digits),
// so the albums stand in the order of `a`. Every album has a cover of its own colour, but album 1.
struct Albums {
    QTemporaryDir dir;
    QList<LibraryTrack> tracks;

    static QColor colourOf(int album) { return QColor(40 + 3 * album, 250 - 4 * album, (album * 37) % 256); }
    QString folderOf(int album) const { return dir.path() + QStringLiteral("/A%1").arg(album, 3, 10, QLatin1Char('0')); }
    void paint(int album, const QColor &colour)
    {
        QImage picture(200, 200, QImage::Format_RGB32);
        picture.fill(colour);
        if (!picture.save(folderOf(album) + QStringLiteral("/cover.png")))
            QTest::qFail("could not write a picture", __FILE__, __LINE__);
    }
    Albums()
    {
        qint64 id = 0;
        for (int a = 0; a < 60; ++a) {
            QDir().mkpath(folderOf(a));
            if (a != 1)
                paint(a, colourOf(a));
            for (int n = 1; n <= 5; ++n) {
                LibraryTrack t;
                t.id = ++id;
                t.path = folderOf(a) + QStringLiteral("/%1.flac").arg(n); // the file need not be there
                t.modified = QDateTime(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);
                t.tags.fields.insert(s(u"Album Artist"), { QStringLiteral("Artist %1").arg(a, 3, 10, QLatin1Char('0')) });
                t.tags.fields.insert(s(u"Album"), { QStringLiteral("Album %1").arg(a, 3, 10, QLatin1Char('0')) });
                t.tags.fields.insert(s(u"Track#"), { QString::number(n) });
                t.tags.fields.insert(s(u"Title"), { QStringLiteral("Title %1").arg(n) });
                t.tags.audio.durationMs = 180000;
                t.tags.format = QStringLiteral("flac");
                tracks.append(t);
            }
        }
    }
};

// A loader that can be held: each load takes one permit before it looks at its file.
struct Gate {
    QSemaphore go;
    CoverCacheHooks hooks()
    {
        CoverCacheHooks h;
        h.loading = [this](const QString &) {
            if (!go.tryAcquire(1, 10000))
                qWarning("the test never let this load go on");
        };
        return h;
    }
};

QTreeWidgetItem *heading(AlbumTracksView &v, int album)
{
    return treeOf(v)->topLevelItem(album);
}

// The headings that are in view: wholly, or at least in part.
QList<int> inView(AlbumTracksView &v, bool wholly)
{
    QTreeWidget *tree = treeOf(v);
    const QRect port = tree->viewport()->rect();
    QList<int> out;
    for (int g = 0; g < tree->topLevelItemCount(); ++g) {
        const QRect at = tree->visualItemRect(tree->topLevelItem(g));
        if (at.isValid() && (wholly ? port.contains(at) : port.intersects(at)))
            out.append(g);
    }
    return out;
}

bool hasCover(AlbumTracksView &v, int album)
{
    return !heading(v, album)->icon(0).isNull();
}

QColor coverColour(AlbumTracksView &v, int album, int edge)
{
    const QImage image = heading(v, album)->icon(0).pixmap(edge, edge).toImage();
    return image.isNull() ? QColor() : image.pixelColor(image.width() / 2, image.height() / 2);
}

// True when every heading wholly in view that has a cover file shows it.
bool allShown(AlbumTracksView &v)
{
    for (const int g : inView(v, true)) {
        if (v.groups().at(g).album != QStringLiteral("Album 001") && !hasCover(v, g))
            return false;
    }
    return true;
}

void settle()
{
    for (int i = 0; i < 4; ++i)
        QCoreApplication::processEvents();
}

#define NEED_COVERS(v, cache) \
    do { \
        NEED_TREE(v); \
        if ((v).coverCache() != &(cache)) \
            QFAIL("the album view does not take a cover cache"); \
    } while (false)

}

class AlbumViewCoversTest : public QObject {
    Q_OBJECT
private slots:
    void theHeadingsInView();
    void scrolling();
    void nothingJumps();
    void aChangeKeepsTheCovers();
    void hiddenAsksNothing();
    void withoutACache();
    void inTheShell();
};

void AlbumViewCoversTest::theHeadingsInView()
{
    // AV-COVER: a fill asks for the covers of the headings in view, one for an album, and for no other.
    (void)&nextRun;
    (void)&named;
    (void)&row;
    (void)&ordered;
    (void)&nine;
    (void)&idsOf;
    Albums a;
    CoverCache cache;
    AlbumTracksView v;
    v.setCoverCache(&cache);
    NEED_COVERS(v, cache);
    v.resize(600, 400);
    v.show();
    QVERIFY(QTest::qWaitForWindowExposed(&v));
    v.setTracks(a.tracks);
    QCOMPARE(cache.loads(), 0); // nothing is asked inside the call
    QTRY_VERIFY_WITH_TIMEOUT(allShown(v), kPatience);
    QTest::qWait(50);
    const QList<int> whole = inView(v, true);
    const QList<int> part = inView(v, false);
    QVERIFY2(whole.size() >= 2 && part.size() < 12, qPrintable(QString::number(part.size())));
    QVERIFY(whole.contains(0) && whole.contains(1));
    // One load for an album in view (its first track), none for an album out of view.
    QVERIFY2(cache.loads() >= whole.size() && cache.loads() <= part.size(), qPrintable(QString::number(cache.loads())));
    for (const int g : whole) {
        if (g == 1)
            continue;
        QVERIFY2(hasCover(v, g), qPrintable(QString::number(g)));
        QCOMPARE(coverColour(v, g, cache.edge()), Albums::colourOf(g)); // each heading its own album's picture
    }
    QVERIFY(!hasCover(v, 1)); // the album without a picture has none, and that was looked up once
    QVERIFY(cache.has(a.tracks.at(5).path, a.tracks.at(5).modified.toMSecsSinceEpoch()));
    QVERIFY(!hasCover(v, 59));
    QVERIFY(!cache.has(a.tracks.at(295).path, a.tracks.at(295).modified.toMSecsSinceEpoch()));
    QCOMPARE(treeOf(v)->iconSize(), QSize(cache.edge(), cache.edge()));

    // The same tracks again: nothing is loaded, and the covers are still there.
    const int loads = cache.loads();
    v.setTracks(a.tracks);
    settle();
    QTest::qWait(50);
    QCOMPARE(cache.loads(), loads);
    QVERIFY(allShown(v));
}

void AlbumViewCoversTest::scrolling()
{
    // AV-COVER: what comes into view is asked for then; what was shown before costs nothing again.
    Albums a;
    CoverCache cache;
    AlbumTracksView v;
    v.setCoverCache(&cache);
    NEED_COVERS(v, cache);
    v.resize(600, 400);
    v.show();
    QVERIFY(QTest::qWaitForWindowExposed(&v));
    v.setTracks(a.tracks);
    QTRY_VERIFY_WITH_TIMEOUT(allShown(v), kPatience);
    QTest::qWait(50);
    const int atTop = cache.loads();
    QScrollBar *bar = treeOf(v)->verticalScrollBar();
    bar->setValue(bar->maximum());
    QTRY_VERIFY_WITH_TIMEOUT(inView(v, true).contains(59) && allShown(v), kPatience);
    QTest::qWait(50);
    QCOMPARE(coverColour(v, 59, cache.edge()), Albums::colourOf(59));
    const int atEnd = cache.loads();
    QVERIFY2(atEnd > atTop && atEnd - atTop <= inView(v, false).size(), qPrintable(QString::number(atEnd - atTop)));
    QVERIFY(!hasCover(v, 30)); // passed over in one jump: never in view, never asked
    // Back to the top: the covers are there at once, and nothing is loaded.
    bar->setValue(0);
    settle();
    QVERIFY(allShown(v));
    QTest::qWait(50);
    QCOMPARE(cache.loads(), atEnd);
    // A taller view shows more albums: they are asked for.
    v.resize(600, 900);
    QTRY_VERIFY_WITH_TIMEOUT(cache.loads() > atEnd, kPatience);
    QTRY_VERIFY_WITH_TIMEOUT(allShown(v), kPatience);
}

void AlbumViewCoversTest::nothingJumps()
{
    // AV-COVER: a heading is as high with its cover as without; a cover that arrives moves no row.
    Albums a;
    Gate gate;
    CoverCache cache(64, gate.hooks());
    AlbumTracksView v;
    v.setCoverCache(&cache);
    NEED_COVERS(v, cache);
    v.resize(600, 400);
    v.show();
    QVERIFY(QTest::qWaitForWindowExposed(&v));
    v.setTracks(a.tracks);
    settle();
    QTreeWidget *tree = treeOf(v);
    QVERIFY(!hasCover(v, 0)); // the loads are held
    QList<QRect> before;
    for (int g = 0; g < 4; ++g)
        before.append(tree->visualItemRect(heading(v, g)));
    QVERIFY2(before.at(0).height() >= cache.edge(), qPrintable(QString::number(before.at(0).height())));
    QCOMPARE(before.at(1).height(), before.at(0).height()); // the album without a picture too
    gate.go.release(100);
    QTRY_VERIFY_WITH_TIMEOUT(allShown(v), kPatience);
    settle();
    for (int g = 0; g < 4; ++g)
        QCOMPARE(tree->visualItemRect(heading(v, g)), before.at(g));
    QCOMPARE(tree->verticalScrollBar()->value(), 0);
    // The heading still reads as before and still spans the view.
    QCOMPARE(heading(v, 0)->text(0), albumHeading(v.groups().at(0)) + QLatin1Char('\n') + albumDetail(v.groups().at(0)));
    QVERIFY(tree->isFirstColumnSpanned(0, QModelIndex()));
}

void AlbumViewCoversTest::aChangeKeepsTheCovers()
{
    // AV-COVER: an album built again shows its cover from the cache; a file that changed is looked up again.
    Albums a;
    CoverCache cache;
    AlbumTracksView v;
    v.setCoverCache(&cache);
    NEED_COVERS(v, cache);
    v.resize(600, 400);
    v.show();
    QVERIFY(QTest::qWaitForWindowExposed(&v));
    v.setTracks(a.tracks);
    QTRY_VERIFY_WITH_TIMEOUT(allShown(v), kPatience);
    QTest::qWait(50);
    const int loads = cache.loads();

    // A title of the first album changes: its rows are built again, its cover costs no load.
    QList<LibraryTrack> changed = a.tracks;
    changed[2].tags.fields.insert(s(u"Title"), { s(u"Another") });
    const int built = v.albumsBuilt();
    v.setTracks(changed);
    QCOMPARE(v.albumsBuilt(), built + 1);
    settle();
    QVERIFY(hasCover(v, 0));
    QCOMPARE(coverColour(v, 0, cache.edge()), Albums::colourOf(0));
    QTest::qWait(50);
    QCOMPARE(cache.loads(), loads);

    // The first track's file changed (the library says so by its time): looked up again, once.
    a.paint(0, Qt::white);
    changed[0].modified = changed.at(0).modified.addSecs(60);
    v.setTracks(changed);
    QTRY_COMPARE_WITH_TIMEOUT(coverColour(v, 0, cache.edge()), QColor(Qt::white), kPatience);
    QTest::qWait(50);
    QCOMPARE(cache.loads(), loads + 1);

    // An album that leaves and comes back: its cover is kept in the cache.
    v.setTracks(changed.mid(5));
    settle();
    v.setTracks(changed);
    QTRY_VERIFY_WITH_TIMEOUT(hasCover(v, 0), kPatience);
    QTest::qWait(50);
    QVERIFY(cache.loads() <= loads + 1 + 2); // at most what newly came into view while it was away
}

void AlbumViewCoversTest::hiddenAsksNothing()
{
    // AV-COVER: a view that is not shown asks for nothing; shown, it asks.
    Albums a;
    CoverCache cache;
    AlbumTracksView v;
    v.setCoverCache(&cache);
    NEED_COVERS(v, cache);
    v.resize(600, 400);
    v.setTracks(a.tracks);
    settle();
    QTest::qWait(100);
    QCOMPARE(cache.loads(), 0);
    QCOMPARE(cache.waiting(), 0);
    v.show();
    QVERIFY(QTest::qWaitForWindowExposed(&v));
    QTRY_VERIFY_WITH_TIMEOUT(cache.loads() > 0 && allShown(v), kPatience);
    QTest::qWait(50);
    const int loads = cache.loads();
    v.hide();
    v.setTracks(a.tracks.mid(100));
    settle();
    QTest::qWait(100);
    QCOMPARE(cache.loads(), loads);
}

void AlbumViewCoversTest::withoutACache()
{
    // AV-COVER: with no cache the view is as it was; a cache that is destroyed is forgotten.
    Albums a;
    AlbumTracksView v;
    NEED_TREE(v);
    QVERIFY(!v.coverCache());
    v.resize(600, 400);
    v.show();
    QVERIFY(QTest::qWaitForWindowExposed(&v));
    v.setTracks(a.tracks);
    settle();
    QTest::qWait(50);
    QVERIFY(!hasCover(v, 0));
    {
        CoverCache cache;
        v.setCoverCache(&cache);
        if (v.coverCache() != &cache)
            QFAIL("the album view does not take a cover cache");
        QTRY_VERIFY_WITH_TIMEOUT(allShown(v), kPatience); // given a cache, the headings in view get their covers
    }
    QVERIFY(!v.coverCache());
    v.setTracks(a.tracks.mid(25)); // no cache any more: this must simply work
    settle();
    QScrollBar *bar = treeOf(v)->verticalScrollBar();
    bar->setValue(bar->maximum());
    settle();
    QCOMPARE(int(v.groups().size()), 55);
    // Taking the cache away by hand does the same.
    CoverCache other;
    v.setCoverCache(&other);
    v.setCoverCache(nullptr);
    QVERIFY(!v.coverCache());
}

void AlbumViewCoversTest::inTheShell()
{
    // AV-SHELL: the album view of the shell has a cover cache.
    Rig r;
    if (!r.shell->navigator())
        QFAIL("plan 30's library shell is not there");
    AlbumViewing viewing(r.shell.get(), &r.settings);
    NEED_VIEW(viewing);
    QVERIFY2(viewing.view()->coverCache(), "the shell gave its album view no cover cache");
    QCOMPARE(viewing.view()->coverCache()->edge(), 64);
}

QTEST_MAIN(AlbumViewCoversTest)
#include "tst_ui_albumview_covers.moc"
