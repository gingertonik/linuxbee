// LOCKED ACCEPTANCE TEST (the album view: every heading keeps the space of a cover). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/album-view.md (AV-COVER, "The
// cover's space"). Runs on the offscreen platform and looks at what the
// view draws: it compares the pixels of heading rows before and after
// their covers arrive. It names no colour of the skin and no font.
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

// Lets every held load go on when it goes out of scope.
struct LetGo {
    Gate &gate;
    ~LetGo() { gate.go.release(1000); }
};

// What the view draws now.
QImage drawn(AlbumTracksView &v)
{
    return treeOf(v)->viewport()->grab().toImage().convertToFormat(QImage::Format_RGB32);
}

// The row of a heading, across the whole view.
QRect rowOf(AlbumTracksView &v, int album)
{
    QTreeWidget *tree = treeOf(v);
    const QRect at = tree->visualItemRect(heading(v, album));
    return QRect(at.left(), at.top(), tree->viewport()->width() - at.left(), at.height());
}

// True when every pixel of the part is the same colour.
bool blank(const QImage &image, const QRect &part)
{
    const QRect in = part.intersected(image.rect());
    if (in.isEmpty())
        return false;
    const QRgb first = image.pixel(in.topLeft());
    for (int y = in.top(); y <= in.bottom(); ++y)
        for (int x = in.left(); x <= in.right(); ++x)
            if (image.pixel(x, y) != first)
                return false;
    return true;
}

// The first column of the row, from its left, that is not all of the colour of the row's first pixel: where the ink starts.
int inkStart(const QImage &image, const QRect &row)
{
    const QRect in = row.intersected(image.rect());
    for (int x = in.left(); x <= in.right(); ++x) {
        if (!blank(image, QRect(x, in.top(), 1, in.height())) || image.pixel(x, in.top()) != image.pixel(in.left(), in.top()))
            return x - in.left();
    }
    return -1;
}

// The space a cover takes at the left of a row, and the rest of the row from a little beyond it.
QRect spaceOf(const QRect &row, int edge)
{
    return QRect(row.left(), row.top(), edge, row.height());
}
QRect restOf(const QRect &row, int edge)
{
    return row.adjusted(edge + 12, 0, 0, 0);
}

}

class AlbumViewCoverSpaceTest : public QObject {
    Q_OBJECT
private slots:
    void theTextStaysWhereItIs();
    void withoutACacheThereIsNoSpace();
};

void AlbumViewCoverSpaceTest::theTextStaysWhereItIs()
{
    // AV-COVER: every heading keeps the cover's space, so its text is at one place before and after the cover comes.
    (void)&ordered;
    (void)&coverColour;
    (void)&nextRun;
    (void)&nine;
    (void)&idsOf;
    (void)&named;
    (void)&row;
    (void)&inView;
    Albums a;
    // Album 1 has no picture. Album 2 has a very small one, album 3 a wide one: neither fills the space.
    QImage small(8, 8, QImage::Format_RGB32);
    small.fill(QColor(200, 30, 30));
    QVERIFY(small.save(a.folderOf(2) + QStringLiteral("/cover.png")));
    QImage wide(200, 50, QImage::Format_RGB32);
    wide.fill(QColor(30, 30, 200));
    QVERIFY(wide.save(a.folderOf(3) + QStringLiteral("/cover.png")));
    Gate gate;
    CoverCache cache(64, gate.hooks());
    const LetGo letGo { gate }; // whatever fails below, the cache is not left waiting
    AlbumTracksView v;
    v.setCoverCache(&cache);
    NEED_COVERS(v, cache);
    v.resize(600, 1600); // tall: four albums with their tracks are wholly in view
    v.show();
    QVERIFY(QTest::qWaitForWindowExposed(&v));
    v.setTracks(a.tracks);
    settle();
    const int edge = cache.edge();
    const QList<int> some { 0, 1, 2, 3 };
    for (const int g : some)
        QVERIFY2(inView(v, true).contains(g), qPrintable(QString::number(g)));

    // Before any cover is there (the loads are held).
    const QImage before = drawn(v);
    QVERIFY(!before.isNull());
    QList<QRect> rows;
    for (const int g : some) {
        QVERIFY(!hasCover(v, g));
        rows.append(rowOf(v, g));
        QVERIFY2(rows.at(g).width() > 3 * edge, qPrintable(QString::number(rows.at(g).width())));
        QVERIFY2(!blank(before, restOf(rows.at(g), edge)), "this test needs text that is drawn; nothing was");
        QVERIFY2(blank(before, spaceOf(rows.at(g), edge)), qPrintable(QStringLiteral("heading %1 draws in the cover's space").arg(g)));
        // The headings all begin with the same letter, so their ink starts at one place.
        QVERIFY2(inkStart(before, rows.at(g)) >= edge, qPrintable(QString::number(inkStart(before, rows.at(g)))));
        QCOMPARE(inkStart(before, rows.at(g)), inkStart(before, rows.at(0)));
    }

    gate.go.release(100);
    QTRY_VERIFY_WITH_TIMEOUT(allShown(v), kPatience);
    QTRY_VERIFY_WITH_TIMEOUT(hasCover(v, 2) && hasCover(v, 3), kPatience);
    settle();
    const QImage after = drawn(v);
    for (const int g : some) {
        QCOMPARE(rowOf(v, g), rows.at(g));
        // Beyond the cover's space nothing has changed: the text has not moved.
        QVERIFY2(after.copy(restOf(rows.at(g), edge)) == before.copy(restOf(rows.at(g), edge)),
            qPrintable(QStringLiteral("the text of heading %1 moved when its cover came").arg(g)));
    }
    // The covers are drawn in the space; the album without one keeps it empty.
    QVERIFY(!blank(after, spaceOf(rows.at(0), edge)));
    QVERIFY(!blank(after, spaceOf(rows.at(2), edge)));
    QVERIFY(!blank(after, spaceOf(rows.at(3), edge)));
    QVERIFY(!hasCover(v, 1));
    QVERIFY(blank(after, spaceOf(rows.at(1), edge)));
    const QPoint middle(rows.at(0).left() + edge / 2, rows.at(0).center().y());
    QCOMPARE(QColor(after.pixel(middle)), Albums::colourOf(0));

    // A fill later on: the headings that come new have the space too.
    QList<LibraryTrack> fewer = a.tracks.mid(5); // album 0 goes; album 1 is the first heading now
    v.setTracks(fewer);
    settle();
    const QImage again = drawn(v);
    QCOMPARE(v.groups().at(0).album, s(u"Album 001"));
    QVERIFY(blank(again, spaceOf(rowOf(v, 0), edge)));
    QCOMPARE(inkStart(again, rowOf(v, 0)), inkStart(before, rows.at(0)));
}

void AlbumViewCoverSpaceTest::withoutACacheThereIsNoSpace()
{
    // AV-COVER: no cache, no covers, no space: the text starts at the left as it always did.
    Albums a;
    AlbumTracksView v;
    NEED_TREE(v);
    v.resize(600, 400);
    v.show();
    QVERIFY(QTest::qWaitForWindowExposed(&v));
    v.setTracks(a.tracks);
    settle();
    const QImage plain = drawn(v);
    const int start = inkStart(plain, rowOf(v, 0));
    QVERIFY2(start >= 0 && start < 32, qPrintable(QString::number(start)));

    // A cache given later makes the space on the headings that are there, at once and before any cover.
    Gate gate;
    {
        CoverCache cache(64, gate.hooks());
        const LetGo letGo { gate };
        v.setCoverCache(&cache);
        NEED_COVERS(v, cache);
        settle();
        const QImage spaced = drawn(v);
        QVERIFY(blank(spaced, spaceOf(rowOf(v, 0), 64)));
        QVERIFY(inkStart(spaced, rowOf(v, 0)) >= 64);
        gate.go.release(100);
        QTRY_VERIFY_WITH_TIMEOUT(hasCover(v, 0), kPatience);
    }
    // The cache is gone: so are the covers and the space, on the headings that are there.
    QVERIFY(!v.coverCache());
    settle();
    QVERIFY(!hasCover(v, 0));
    QVERIFY(!hasCover(v, 2));
    QCOMPARE(inkStart(drawn(v), rowOf(v, 0)), start);
    // And taken away by hand, the same.
    CoverCache second(64);
    v.setCoverCache(&second);
    QTRY_VERIFY_WITH_TIMEOUT(hasCover(v, 0), kPatience); // the same picture again, from another cache
    QVERIFY(!blank(drawn(v), spaceOf(rowOf(v, 0), 64))); // drawn in its space
    v.setCoverCache(nullptr);
    settle();
    QVERIFY(!hasCover(v, 0));
    QCOMPARE(inkStart(drawn(v), rowOf(v, 0)), start);
}

QTEST_MAIN(AlbumViewCoverSpaceTest)
#include "tst_ui_albumview_coverspace.moc"
