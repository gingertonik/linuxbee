// LOCKED ACCEPTANCE TEST (the album view: one change, one album). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/album-view.md (AV-ORDER, AV-VIEW,
// AV-FILL). What a view must show after a change is what a second view shows
// that is given the same tracks afresh. Runs on the offscreen platform, with a
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
#include "sort/fieldsort.h"
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

// 60 albums of 5 tracks, ids 1 to 300, in the order of the view; album `a` is
// by "Artist a" (three digits), so the albums stand in the order of `a`.
LibraryTrack one(qint64 id, int album, int number)
{
    LibraryTrack t;
    t.id = id;
    t.path = QStringLiteral("/music/%1/%2.flac").arg(album, 3, 10, QLatin1Char('0')).arg(id);
    t.tags.fields.insert(s(u"Album Artist"), { QStringLiteral("Artist %1").arg(album, 3, 10, QLatin1Char('0')) });
    t.tags.fields.insert(s(u"Album"), { QStringLiteral("Album %1").arg(album, 3, 10, QLatin1Char('0')) });
    t.tags.fields.insert(s(u"Year"), { QString::number(1960 + album) });
    t.tags.fields.insert(s(u"Track#"), { QString::number(number) });
    t.tags.fields.insert(s(u"Title"), { QStringLiteral("Title %1").arg(number) });
    t.tags.audio.durationMs = 180000;
    t.tags.format = QStringLiteral("flac");
    return t;
}

QList<LibraryTrack> sixty()
{
    QList<LibraryTrack> out;
    qint64 id = 0;
    for (int a = 0; a < 60; ++a) {
        for (int n = 1; n <= 5; ++n) {
            ++id;
            out.append(one(id, a, n));
        }
    }
    return out;
}

// The same tracks with one field of one track set (an empty value takes the field away).
QList<LibraryTrack> with(QList<LibraryTrack> tracks, qint64 id, const char16_t *field, const QString &value)
{
    for (LibraryTrack &t : tracks) {
        if (t.id != id)
            continue;
        if (value.isEmpty())
            t.tags.fields.remove(s(field));
        else
            t.tags.fields.insert(s(field), { value });
    }
    return tracks;
}

LibraryTrack with(const LibraryTrack &track, const char16_t *field, const QString &value)
{
    return with(QList<LibraryTrack>({ track }), track.id, field, value).first();
}

// Everything the tree shows, row by row.
QStringList dump(AlbumTracksView &v)
{
    QStringList out;
    QTreeWidget *tree = treeOf(v);
    for (int g = 0; g < tree->topLevelItemCount(); ++g) {
        QTreeWidgetItem *head = tree->topLevelItem(g);
        out.append(QStringLiteral("H|%1|%2|%3|%4")
                       .arg(head->text(0))
                       .arg(tree->isFirstColumnSpanned(g, QModelIndex()))
                       .arg(head->isExpanded())
                       .arg(head->childCount()));
        const QModelIndex parent = tree->model()->index(g, 0);
        for (int i = 0; i < head->childCount(); ++i) {
            const QTreeWidgetItem *child = head->child(i);
            out.append(QStringLiteral("R|%1|%2|%3|%4|%5|%6")
                           .arg(child->text(0), child->text(1), child->text(2), child->toolTip(1))
                           .arg(bool(child->flags() & Qt::ItemIsSelectable))
                           .arg(tree->isFirstColumnSpanned(i, parent)));
        }
    }
    return out;
}

// "" when the view shows what a view given the same tracks afresh shows.
QString asAfresh(AlbumTracksView &v, const QList<LibraryTrack> &tracks)
{
    AlbumTracksView other;
    other.setTracks(tracks);
    if (v.groups() != other.groups())
        return QStringLiteral("groups() differs from that of a view filled afresh");
    const QStringList mine = dump(v);
    const QStringList fresh = dump(other);
    if (mine.size() != fresh.size())
        return QStringLiteral("%1 rows, and %2 in a view filled afresh").arg(mine.size()).arg(fresh.size());
    for (qsizetype i = 0; i < mine.size(); ++i) {
        if (mine.at(i) != fresh.at(i))
            return QStringLiteral("row %1: [%2], afresh [%3]").arg(i).arg(mine.at(i), fresh.at(i));
    }
    return QString();
}

}

class AlbumViewOneTest : public QObject {
    Q_OBJECT
private slots:
    void theOrderKeepsItsKeys();
    void theViewBuildsWhatDiffers();
    void theHeadingsKeepTheirNumbers();
    void oneChangeInTheShell();
};

void AlbumViewOneTest::theOrderKeepsItsKeys()
{
    // AV-ORDER: AlbumOrder gives the order of sortedForAlbums and prepares a value only for what is new to it.
    (void)&nextRun;
    (void)&named;
    (void)&row;
    (void)&ordered;
    (void)&nine;
    const qint64 keys = albumSortFields().size();
    QList<LibraryTrack> tracks = sixty();
    std::reverse(tracks.begin(), tracks.end()); // given out of order
    AlbumOrder order;
    qint64 prepared = 0;
    qint64 compared = fieldValueComparisons();
    const auto mark = [&prepared] { prepared = fieldValuesPrepared(); };
    const auto made = [&prepared] { return fieldValuesPrepared() - prepared; };
    const auto same = [&order](const QList<LibraryTrack> &given) {
        return idsOf(order.sorted(given)) == idsOf(sortedForAlbums(given));
    };

    mark();
    const QList<LibraryTrack> first = order.sorted(tracks);
    QVERIFY2(made() > 0 && made() <= 300 * keys, qPrintable(QString::number(made())));
    QCOMPARE(idsOf(first), idsOf(sixty()));

    // The same tracks again, and again in another order: nothing is prepared.
    mark();
    QCOMPARE(idsOf(order.sorted(tracks)), idsOf(sixty()));
    QCOMPARE(made(), 0);
    mark();
    QCOMPARE(idsOf(order.sorted(sixty())), idsOf(sixty()));
    QCOMPARE(made(), 0);

    // One title changed: that track's keys and no other's.
    tracks = with(tracks, 152, u"Title", s(u"Aaa"));
    mark();
    const QList<LibraryTrack> afterTitle = order.sorted(tracks);
    QVERIFY2(made() > 0 && made() <= keys, qPrintable(QString::number(made())));
    QCOMPARE(idsOf(afterTitle), idsOf(sortedForAlbums(tracks)));
    for (const LibraryTrack &t : afterTitle) {
        if (t.id == 152)
            QCOMPARE(t.tags.fields.value(s(u"Title")), QStringList({ s(u"Aaa") })); // the track as it is now
    }

    // A change that is in no key: nothing is prepared, and the track handed back is the new one.
    tracks = with(tracks, 152, u"Genre", s(u"Jazz"));
    for (LibraryTrack &t : tracks) {
        if (t.id == 9)
            t.playCount = 7;
    }
    mark();
    const QList<LibraryTrack> afterGenre = order.sorted(tracks);
    QCOMPARE(made(), 0);
    QCOMPARE(idsOf(afterGenre), idsOf(sortedForAlbums(tracks)));
    for (const LibraryTrack &t : afterGenre) {
        if (t.id == 152)
            QCOMPARE(t.tags.fields.value(s(u"Genre")), QStringList({ s(u"Jazz") }));
        if (t.id == 9)
            QCOMPARE(t.playCount, 7);
    }

    // The album artist goes and the artist stands in (AB-ARTIST); a year written as a date is the same year.
    tracks = with(with(tracks, 40, u"Album Artist", QString()), 40, u"Artist", s(u"Zzz"));
    QVERIFY(same(tracks));
    tracks = with(tracks, 41, u"Year", s(u"1968-02-03"));
    QVERIFY(same(tracks));

    // Many at once, a track that is new, and one that is gone.
    for (qint64 id = 201; id <= 300; ++id)
        tracks = with(tracks, id, u"Album", QStringLiteral("Other %1").arg(id % 4));
    tracks.append(one(900, 30, 9));
    tracks.removeIf([](const LibraryTrack &t) { return t.id == 77; });
    mark();
    const QList<LibraryTrack> afterMany = order.sorted(tracks);
    QVERIFY2(made() > 0 && made() <= 101 * keys, qPrintable(QString::number(made())));
    QCOMPARE(idsOf(afterMany), idsOf(sortedForAlbums(tracks)));
    QCOMPARE(afterMany.size(), 300);

    // A path decides between tracks alike in every key, so a path that changed is looked at.
    for (LibraryTrack &t : tracks) {
        if (t.id == 3) {
            t.tags.fields = one(4, 0, 4).tags.fields;
            t.path = QStringLiteral("/music/000/zzz.flac");
        }
    }
    QVERIFY(same(tracks));
    for (LibraryTrack &t : tracks) {
        if (t.id == 3)
            t.path = QStringLiteral("/music/000/0.flac");
    }
    QVERIFY(same(tracks));

    // What is not given is forgotten: given again, it is prepared again.
    QList<LibraryTrack> few = tracks.mid(0, 10);
    QVERIFY(same(few));
    mark();
    QCOMPARE(order.sorted(tracks).size(), 300);
    QVERIFY2(made() >= 290 && made() <= 290 * keys, qPrintable(QString::number(made())));
    QCOMPARE(order.sorted({ }).size(), 0);

    // The options are those it was made with.
    SortOptions words;
    words.ignoreWords = { s(u"Artist") };
    AlbumOrder worded(words);
    QList<LibraryTrack> mixed = with(sixty(), 1, u"Album Artist", s(u"Artist Zebra"));
    mixed = with(mixed, 2, u"Album Artist", s(u"Alpha"));
    QCOMPARE(idsOf(worded.sorted(mixed)), idsOf(sortedForAlbums(mixed, words)));
    QVERIFY(idsOf(sortedForAlbums(mixed, words)) != idsOf(sortedForAlbums(mixed)));
    QCOMPARE(fieldValueComparisons(), compared); // nothing here compares unprepared values
}

void AlbumViewOneTest::theViewBuildsWhatDiffers()
{
    // AV-VIEW: setTracks builds the rows of the albums that differ and leaves the others.
    AlbumTracksView v;
    NEED_TREE(v);
    QList<LibraryTrack> tracks = sixty();
    QCOMPARE(v.albumsBuilt(), 0);
    v.setTracks(tracks);
    QCOMPARE(v.albumsBuilt(), 60);
    QCOMPARE(v.fills(), 1);
    QCOMPARE(asAfresh(v, tracks), QString());
    int built = v.albumsBuilt();
    const auto more = [&v, &built] {
        const int now = v.albumsBuilt() - built;
        built = v.albumsBuilt();
        return now;
    };

    // The same tracks: a fill that builds nothing.
    v.setTracks(tracks);
    QCOMPARE(v.fills(), 2);
    QCOMPARE(more(), 0);
    QCOMPARE(asAfresh(v, tracks), QString());

    // A selection in an album that stays and in one that is built again, kept through the changes below.
    v.selectTracks({ 12, 152 });
    QSignalSpy selection(&v, &AlbumTracksView::selectionChanged);

    // One title: one album.
    tracks = with(tracks, 152, u"Title", s(u"Another title"));
    v.setTracks(tracks);
    QCOMPARE(more(), 1);
    QCOMPARE(asAfresh(v, tracks), QString());
    QCOMPARE(row(v, 152)->text(1), s(u"Another title"));
    QCOMPARE(v.selectedTrackIds(), QList<qint64>({ 12, 152 }));
    QCOMPARE(selection.count(), 0);

    // A track number, a length, a path (the tool tip), a disc: each one album.
    tracks = with(tracks, 7, u"Track#", s(u"1"));
    v.setTracks(tracks);
    QCOMPARE(more(), 1);
    QCOMPARE(asAfresh(v, tracks), QString());
    for (LibraryTrack &t : tracks) {
        if (t.id == 290)
            t.tags.audio.durationMs = 61000;
    }
    v.setTracks(tracks);
    QCOMPARE(more(), 1);
    QCOMPARE(asAfresh(v, tracks), QString());
    for (LibraryTrack &t : tracks) {
        if (t.id == 1)
            t.path = QStringLiteral("/music/elsewhere.flac");
    }
    v.setTracks(tracks);
    QCOMPARE(more(), 1);
    QCOMPARE(asAfresh(v, tracks), QString());
    tracks = with(with(with(tracks, 104, u"Disc#", s(u"2")), 105, u"Disc#", s(u"2")), 103, u"Disc#", s(u"1"));
    v.setTracks(tracks);
    QCOMPARE(more(), 1);
    QCOMPARE(asAfresh(v, tracks), QString());

    // A change that is in no tag builds nothing.
    for (LibraryTrack &t : tracks) {
        if (t.id == 9)
            t.playCount = 7;
    }
    v.setTracks(tracks);
    QCOMPARE(more(), 0);

    // A genre is in the heading's second line: one album.
    tracks = with(tracks, 200, u"Genre", s(u"Jazz"));
    v.setTracks(tracks);
    QVERIFY(more() <= 1);
    QCOMPARE(asAfresh(v, tracks), QString());

    // A track goes from album 10 to album 40: no album before the first or after the second is built.
    QList<LibraryTrack> moved = with(with(with(tracks, 53, u"Album Artist", s(u"Artist 040")), 53, u"Album", s(u"Album 040")), 53, u"Year", s(u"2000"));
    moved = sortedForAlbums(moved);
    v.setTracks(moved);
    const int between = more();
    QVERIFY2(between >= 2 && between <= 31, qPrintable(QString::number(between)));
    QCOMPARE(asAfresh(v, moved), QString());
    QCOMPARE(v.selectedTrackIds(), QList<qint64>({ 12, 152 }));
    QCOMPARE(selection.count(), 0);

    // Fewer, none, and all again.
    v.setTracks(moved.mid(0, 100));
    QCOMPARE(asAfresh(v, moved.mid(0, 100)), QString());
    QVERIFY(more() <= 1); // the first twenty albums are as they were
    v.setTracks({ });
    QCOMPARE(treeOf(v)->topLevelItemCount(), 0);
    QCOMPARE(more(), 0);
    v.setTracks(moved);
    QCOMPARE(more(), int(v.groups().size()));
    QCOMPARE(asAfresh(v, moved), QString());
}

void AlbumViewOneTest::theHeadingsKeepTheirNumbers()
{
    // AV-VIEW: an album that is left as it is still answers with its place among the albums now.
    AlbumTracksView v;
    NEED_TREE(v);
    v.resize(600, 400);
    v.show();
    QList<LibraryTrack> tracks = sixty();
    v.setTracks(tracks);
    QTreeWidget *tree = treeOf(v);
    QSignalSpy album(&v, &AlbumTracksView::albumActivated);
    int built = v.albumsBuilt();

    // The first album goes: the others are one place up, and none is built.
    tracks = tracks.mid(5);
    v.setTracks(tracks);
    QCOMPARE(v.albumsBuilt() - built, 0);
    QCOMPARE(asAfresh(v, tracks), QString());
    tree->setCurrentItem(row(v, 300), 0, QItemSelectionModel::NoUpdate);
    QCOMPARE(v.currentGroup(), 58);
    tree->setCurrentItem(row(v, -59));
    QTest::keyClick(tree, Qt::Key_Return);
    QCOMPARE(album.count(), 1);
    QCOMPARE(album.at(0).at(0).toInt(), 58);
    // A selected heading stands for the tracks of the album it heads now.
    QCOMPARE(v.selectedTrackIds(), QList<qint64>({ 296, 297, 298, 299, 300 }));
    // A track of the album that went has no row any more.
    v.selectTracks({ 3, 300 });
    QCOMPARE(v.selectedTrackIds(), QList<qint64>({ 300 }));

    // Two albums come at the top and one in the middle goes: built are the two.
    built = v.albumsBuilt();
    QList<LibraryTrack> wider;
    wider.append(with(one(901, 0, 1), u"Album Artist", s(u"Aaa 1")));
    wider.append(with(one(902, 0, 1), u"Album Artist", s(u"Aaa 2")));
    for (const LibraryTrack &t : std::as_const(tracks)) {
        if (t.id < 101 || t.id > 105)
            wider.append(t);
    }
    wider = sortedForAlbums(wider);
    v.setTracks(wider);
    QCoreApplication::processEvents();
    QCOMPARE(tree->topLevelItem(0), tree->itemAt(QPoint(2, 2))); // a fill still shows the top (AV-VIEW)
    const int came = v.albumsBuilt() - built;
    QVERIFY2(came >= 2 && came <= 21, qPrintable(QString::number(came))); // at most all from the top to the one that went
    QCOMPARE(asAfresh(v, wider), QString());
    QCOMPARE(int(v.groups().size()), 60);
    tree->setCurrentItem(row(v, 300), 0, QItemSelectionModel::NoUpdate);
    QCOMPARE(v.currentGroup(), 59);
    tree->setCurrentItem(row(v, 6), 0, QItemSelectionModel::NoUpdate);
    QCOMPARE(v.currentGroup(), 2);
    v.selectTracks({ 300, 6 });
    QCOMPARE(v.selectedTrackIds(), QList<qint64>({ 6, 300 }));
    v.selectTracks({ 103, 2 });
    QCOMPARE(v.selectedTrackIds(), QList<qint64>());
}

void AlbumViewOneTest::oneChangeInTheShell()
{
    // AV-FILL: a track that changed in the library costs the album view no key and, when nothing it shows changed, no album.
    Rig r;
    if (!r.shell->navigator())
        QFAIL("plan 30's library shell is not there");
    AlbumViewing viewing(r.shell.get(), &r.settings);
    NEED_VIEW(viewing);
    QCoreApplication::processEvents();
    const TrackListModel *model = r.shell->model();
    QCOMPARE(model->rowCount(), 4);
    const qint64 id = model->trackId(2);
    const int built = viewing.view()->albumsBuilt();
    const qint64 prepared = fieldValuesPrepared();

    TrackStatistics statistics;
    statistics.dateAdded = r.now;
    statistics.playCount = 5;
    QVERIFY(r.library->setStatistics(id, statistics));
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    AlbumTracksView *view = viewing.view();
    // The list may sort its four rows again (T-MODEL); the album view prepares nothing on top of that.
    const qint64 made = fieldValuesPrepared() - prepared;
    QVERIFY2(made <= 4 * model->sortFields().size(), qPrintable(QString::number(made)));
    QCOMPARE(view->albumsBuilt(), built);
    QStringList paths;
    for (const AlbumGroup &g : view->groups())
        paths += g.paths;
    paths.sort();
    QCOMPARE(paths, r.shown());

    // Another node and back: the order is right each time.
    r.shell->trackList()->setSearchText(s(u"adel"));
    QVERIFY(viewing.view()->groups().size() < 4);
    r.shell->trackList()->setSearchText(QString());
    QList<qint64> shownIds;
    for (const AlbumGroup &g : viewing.view()->groups())
        shownIds += g.trackIds;
    QCOMPARE(shownIds.size(), 4);
    QCOMPARE(asAfresh(*viewing.view(), sortedForAlbums(r.shell->nodeTracks())), QString());
}

QTEST_MAIN(AlbumViewOneTest)
#include "tst_ui_albumview_one.moc"
