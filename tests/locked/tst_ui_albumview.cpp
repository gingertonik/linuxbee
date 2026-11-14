// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/album-view.md (AV-x). Runs on the
// offscreen platform. The last tests use a real library (in memory), the
// real window and the real library shell.
#include <QBoxLayout>
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

}

class AlbumViewTest : public QObject {
    Q_OBJECT
private slots:
    void order();
    void grouping();
    void texts();
    void jumping();
    void rows();
    void selecting();
    void activating();
    void jumpBar();
    void inTheShell();
    void selectionAndPlay();
    void modes();
};

void AlbumViewTest::order()
{
    // AV-ORDER
    QCOMPARE(albumSortFields(),
        QStringList({ s(u"Album Artist"), s(u"Year"), s(u"Album"), s(u"Disc#"), s(u"Track#"), s(u"Title") }));
    // Adele (by her artist, having no album artist; the track with no year last), The Beatles
    // under B (disc 1 before disc 2), Boards of Canada by year and not by album name, no artist last.
    QCOMPARE(idsOf(sortedForAlbums(nine())), QList<qint64>({ 7, 9, 4, 6, 5, 1, 2, 3, 8 }));
    QList<LibraryTrack> reversed = nine();
    std::reverse(reversed.begin(), reversed.end());
    QCOMPARE(idsOf(sortedForAlbums(reversed)), QList<qint64>({ 7, 9, 4, 6, 5, 1, 2, 3, 8 }));
    // Without the ignored word, The Beatles stand under T, after Boards of Canada.
    SortOptions strict;
    strict.ignoreWords.clear();
    QCOMPARE(idsOf(sortedForAlbums(nine(), strict)), QList<qint64>({ 7, 9, 1, 2, 3, 4, 6, 5, 8 }));
    // Tracks alike in every key: by path.
    LibraryTrack a = made({ 21, u"X", u"X", u"Y", u"2000", u"1", u"1", u"Same", 1, u"" });
    LibraryTrack b = a;
    b.id = 20;
    b.path = QStringLiteral("/music/zz.flac");
    QCOMPARE(idsOf(sortedForAlbums({ b, a })), QList<qint64>({ 21, 20 }));
    QCOMPARE(sortedForAlbums({ }).size(), 0);
}

void AlbumViewTest::grouping()
{
    // AV-GROUPS
    const QList<AlbumGroup> groups = albumGroups(sortedForAlbums(nine()));
    QCOMPARE(groups.size(), 6);
    AlbumGroup want;
    want.artist = s(u"Adele");
    want.album = s(u"21");
    want.year = s(u"2011");
    want.genre = s(u"Pop");
    want.totalMs = 228000;
    want.trackIds = { 7 };
    want.paths = { s(u"/music/07.flac") };
    QCOMPARE(groups.value(0), want);
    QCOMPARE(groups.value(1).album, QString()); // her track with no album: a group of its own
    QCOMPARE(groups.value(1).trackIds, QList<qint64>({ 9 }));
    const AlbumGroup beatles = groups.value(2);
    QCOMPARE(beatles.artist, s(u"The Beatles"));
    QCOMPARE(beatles.trackIds, QList<qint64>({ 4, 6, 5 }));
    QCOMPARE(beatles.paths, QStringList({ s(u"/music/04.flac"), s(u"/music/06.flac"), s(u"/music/05.flac") }));
    QCOMPARE(beatles.discStarts, QList<int>({ 2 })); // the third track is on disc 2
    QCOMPARE(beatles.genre, s(u"Rock")); // of the first track that has one
    QCOMPARE(beatles.totalMs, qint64(464000));
    QCOMPARE(groups.value(3).year, s(u"1998")); // from 1998-04-20
    QCOMPARE(groups.value(3).genre, s(u"Electronic"));
    QCOMPARE(groups.value(3).discStarts, QList<int>());
    QCOMPARE(groups.value(4).album, s(u"Geogaddi"));
    QCOMPARE(groups.value(5).artist, QString());
    QCOMPARE(groups.value(5).album, QString());

    // Nothing is reordered: an album met again is a second group. Case and spaces do not split one.
    const QList<AlbumGroup> asGiven = albumGroups({
        made({ 1, u"A", u"", u"One", u"", u"", u"", u"t", 0, u"" }),
        made({ 2, u"a ", u"", u" one", u"", u"", u"", u"t", 0, u"" }),
        made({ 3, u"A", u"", u"Two", u"", u"", u"", u"t", 0, u"" }),
        made({ 4, u"A", u"", u"One", u"", u"", u"", u"t", 0, u"" }),
        made({ 5, u"B", u"", u"One", u"", u"", u"", u"t", 0, u"" }), // the same album name, another artist
    });
    QCOMPARE(asGiven.size(), 4);
    QCOMPARE(asGiven.value(0).trackIds, QList<qint64>({ 1, 2 }));
    QCOMPARE(asGiven.value(0).album, s(u"One")); // as the first track spells it
    QCOMPARE(asGiven.value(2).trackIds, QList<qint64>({ 4 }));
    QCOMPARE(asGiven.value(3).trackIds, QList<qint64>({ 5 }));
    // Discs: every change counts, also back to an earlier one, and from none to one.
    const QList<AlbumGroup> discs = albumGroups({
        made({ 1, u"A", u"", u"One", u"", u"", u"", u"t", 0, u"" }),
        made({ 2, u"A", u"", u"One", u"", u"1", u"", u"t", 0, u"" }),
        made({ 3, u"A", u"", u"One", u"", u" 1 ", u"", u"t", 0, u"" }),
        made({ 4, u"A", u"", u"One", u"", u"2", u"", u"t", 0, u"" }),
        made({ 5, u"A", u"", u"One", u"", u"1", u"", u"t", 0, u"" }),
    });
    QCOMPARE(discs.size(), 1);
    QCOMPARE(discs.value(0).discStarts, QList<int>({ 1, 3, 4 }));
    QCOMPARE(albumGroups({ }).size(), 0);
}

void AlbumViewTest::texts()
{
    // AV-TEXT
    const QList<AlbumGroup> groups = albumGroups(sortedForAlbums(nine()));
    QCOMPARE(groups.size(), 6);
    QCOMPARE(albumHeading(groups.value(2)), s(u"Abbey Road"));
    QCOMPARE(albumDetail(groups.value(2)), s(u"The Beatles \u2022 1969 \u2022 Rock \u2022 3 tracks \u2022 8 min"));
    QCOMPARE(albumDetail(groups.value(0)), s(u"Adele \u2022 2011 \u2022 Pop \u2022 1 track \u2022 4 min"));
    QCOMPARE(albumHeading(groups.value(1)), s(u"[Unknown Album]"));
    QCOMPARE(albumDetail(groups.value(1)), s(u"Adele \u2022 1 track \u2022 0 min"));
    QCOMPARE(albumDetail(groups.value(5)), s(u"[Unknown Artist] \u2022 1 track \u2022 0 min"));
    AlbumGroup g = named(u"A");
    for (const auto &[ms, want] : { std::pair { 89999, u"A \u2022 1 track \u2022 1 min" }, std::pair { 90000, u"A \u2022 1 track \u2022 2 min" },
             std::pair { 29999, u"A \u2022 1 track \u2022 0 min" }, std::pair { 3780000, u"A \u2022 1 track \u2022 63 min" } }) {
        g.totalMs = ms;
        QCOMPARE(albumDetail(g), s(want));
    }
}

void AlbumViewTest::jumping()
{
    // AV-JUMP
    const QList<AlbumGroup> groups = albumGroups(sortedForAlbums(nine()));
    QCOMPARE(groups.size(), 6);
    QCOMPARE(jumpIndex(groups, QLatin1Char('A')), 0);
    QCOMPARE(jumpIndex(groups, QLatin1Char('B')), 2); // The Beatles, the first under B
    QCOMPARE(jumpIndex(groups, QLatin1Char('b')), 2);
    QCOMPARE(jumpIndex(groups, QLatin1Char('#')), 5); // the group with no artist
    QCOMPARE(jumpIndex(groups, QLatin1Char('T')), -1);
    QCOMPARE(jumpIndex(groups, QLatin1Char('Z')), -1);
    QCOMPARE(jumpIndex(groups, QLatin1Char('1')), -1);
    QCOMPARE(jumpIndex(groups, QLatin1Char(' ')), -1);
    QCOMPARE(jumpIndex({ }, QLatin1Char('A')), -1);
    SortOptions strict;
    strict.ignoreWords.clear();
    QCOMPARE(jumpIndex(groups, QLatin1Char('T'), strict), 2);
    QCOMPARE(jumpIndex(groups, QLatin1Char('B'), strict), 3);
    const QList<AlbumGroup> odd { named(u"2Pac"), named(u"  the  Who"), named(u"\u00c9milie Simon"), named(u"Theory"),
        named(u"The"), named(u"\u00f6de") };
    QCOMPARE(jumpIndex(odd, QLatin1Char('#')), 0);
    QCOMPARE(jumpIndex(odd, QLatin1Char('W')), 1); // "the" and the spaces after it dropped, whatever the case
    QCOMPARE(jumpIndex(odd, QLatin1Char('E')), 2); // accents folded
    QCOMPARE(jumpIndex(odd, QLatin1Char('T')), 3); // "Theory" does not begin with the word "The"; neither is "The" alone dropped
    QCOMPARE(jumpIndex(odd, QLatin1Char('O')), 5);
}

void AlbumViewTest::rows()
{
    // AV-VIEW
    AlbumTracksView v;
    NEED_TREE(v);
    QTreeWidget *tree = treeOf(v);
    QCOMPARE(tree->columnCount(), 3);
    QVERIFY(tree->isHeaderHidden());
    QVERIFY(!tree->rootIsDecorated());
    QVERIFY(!tree->itemsExpandable());
    QCOMPARE(tree->topLevelItemCount(), 0);
    QCOMPARE(v.groups().size(), 0);
    QCOMPARE(v.currentGroup(), -1);

    const QList<LibraryTrack> tracks = sortedForAlbums(nine());
    v.setTracks(tracks);
    QCOMPARE(v.groups(), albumGroups(tracks));
    QCOMPARE(tree->topLevelItemCount(), 6);
    for (int g = 0; g < 6; ++g) {
        const QTreeWidgetItem *head = tree->topLevelItem(g);
        QCOMPARE(head->text(0), albumHeading(v.groups().at(g)) + QLatin1Char('\n') + albumDetail(v.groups().at(g)));
        QVERIFY(head->isFirstColumnSpanned());
        QVERIFY(head->isExpanded());
    }
    // The Beatles: two tracks, the disc row, one track.
    const QTreeWidgetItem *beatles = tree->topLevelItem(2);
    QCOMPARE(beatles->childCount(), 4);
    QCOMPARE(beatles->child(0)->text(0), s(u"1"));
    QCOMPARE(beatles->child(0)->text(1), s(u"Come Together"));
    QCOMPARE(beatles->child(0)->text(2), LibraryFieldSource(tracks.at(2)).value(s(u"Time")).value_or(s(u"?")));
    QVERIFY(!beatles->child(0)->text(2).isEmpty());
    QCOMPARE(beatles->child(0)->toolTip(1), s(u"/music/04.flac"));
    QCOMPARE(beatles->child(1)->text(1), s(u"Something"));
    QCOMPARE(beatles->child(2)->text(0), s(u"Disc 2"));
    QVERIFY(beatles->child(2)->isFirstColumnSpanned());
    QVERIFY(!(beatles->child(2)->flags() & Qt::ItemIsSelectable));
    QCOMPARE(beatles->child(3)->text(1), s(u"Her Majesty"));
    QVERIFY(beatles->child(3)->flags() & Qt::ItemIsSelectable);
    // No title: the file's name.
    QCOMPARE(tree->topLevelItem(1)->childCount(), 1);
    QCOMPARE(tree->topLevelItem(1)->child(0)->text(1), s(u"09.flac"));
    QCOMPARE(tree->topLevelItem(1)->child(0)->text(0), QString());
    // Filled again with fewer.
    v.setTracks(tracks.mid(5));
    QCOMPARE(tree->topLevelItemCount(), 3);
    v.setTracks({ });
    QCOMPARE(tree->topLevelItemCount(), 0);
}

void AlbumViewTest::selecting()
{
    // AV-SELECT
    AlbumTracksView v;
    NEED_TREE(v);
    const QList<LibraryTrack> tracks = sortedForAlbums(nine());
    v.setTracks(tracks);
    QCOMPARE(treeOf(v)->selectionMode(), QAbstractItemView::ExtendedSelection);
    QSignalSpy changed(&v, &AlbumTracksView::selectionChanged);
    QCOMPARE(v.selectedTrackIds(), QList<qint64>());
    QVERIFY(row(v, -3) && row(v, 7) && row(v, 6));
    row(v, -3)->setSelected(true); // the heading of Abbey Road stands for its tracks
    QCOMPARE(v.selectedTrackIds(), QList<qint64>({ 4, 6, 5 }));
    QCOMPARE(changed.count(), 1);
    row(v, 6)->setSelected(true); // one of them as well: each once
    QCOMPARE(v.selectedTrackIds(), QList<qint64>({ 4, 6, 5 }));
    QCOMPARE(changed.count(), 1); // the answer did not change
    row(v, 7)->setSelected(true);
    QCOMPARE(v.selectedTrackIds(), QList<qint64>({ 7, 4, 6, 5 })); // in the order shown
    QCOMPARE(changed.count(), 2);

    v.selectTracks({ 2, 99, 4 });
    QCOMPARE(v.selectedTrackIds(), QList<qint64>({ 4, 2 }));
    QVERIFY(!row(v, -3)->isSelected());
    QCOMPARE(changed.count(), 3);
    v.selectTracks({ 4, 2 }); // the same
    QCOMPARE(changed.count(), 3);
    v.setTracks(tracks); // filled again: kept, and no signal
    QCOMPARE(v.selectedTrackIds(), QList<qint64>({ 4, 2 }));
    QCOMPARE(changed.count(), 3);
    QList<LibraryTrack> fewer = tracks;
    fewer.removeAt(2); // id 4 goes
    v.setTracks(fewer);
    QCOMPARE(v.selectedTrackIds(), QList<qint64>({ 2 }));
    QCOMPARE(changed.count(), 4);
    v.selectTracks({ });
    QCOMPARE(v.selectedTrackIds(), QList<qint64>());
    QCOMPARE(changed.count(), 5);
}

void AlbumViewTest::activating()
{
    // AV-ACTIVATE
    AlbumTracksView v;
    NEED_TREE(v);
    QTreeWidget *tree = treeOf(v);
    v.setTracks(sortedForAlbums(nine()));
    v.resize(700, 900);
    v.show();
    QVERIFY(QTest::qWaitForWindowExposed(&v));
    QSignalSpy track(&v, &AlbumTracksView::trackActivated);
    QSignalSpy album(&v, &AlbumTracksView::albumActivated);
    QSignalSpy menu(&v, &AlbumTracksView::menuRequested);
    const auto centre = [tree](QTreeWidgetItem *item, int column) { return tree->visualItemRect(item).isValid() ? tree->visualRect(tree->indexFromItem(item, column)).center() : QPoint(-1, -1); };
    QVERIFY(row(v, 6) && row(v, -3));
    QTreeWidgetItem *disc = tree->topLevelItem(2)->child(2);

    QTest::mouseClick(tree->viewport(), Qt::LeftButton, { }, centre(row(v, 6), 1));
    QCOMPARE(v.selectedTrackIds(), QList<qint64>({ 6 }));
    QCOMPARE(track.count(), 0); // one click selects
    QTest::mouseDClick(tree->viewport(), Qt::LeftButton, { }, centre(row(v, 6), 1));
    QCOMPARE(track.count(), 1);
    QCOMPARE(track.at(0).at(0).toLongLong(), qint64(6));
    QCOMPARE(v.currentGroup(), 2);
    QTest::keyClick(tree, Qt::Key_Return);
    QCOMPARE(track.count(), 2);
    QTest::keyClick(tree, Qt::Key_Enter);
    QCOMPARE(track.count(), 3);
    QCOMPARE(album.count(), 0);

    QTest::mouseDClick(tree->viewport(), Qt::LeftButton, { }, centre(row(v, -3), 0));
    QCOMPARE(album.count(), 1);
    QCOMPARE(album.at(0).at(0).toInt(), 2);
    QVERIFY(row(v, -3)->isExpanded()); // a heading does not fold
    tree->setCurrentItem(row(v, -5));
    QTest::keyClick(tree, Qt::Key_Return);
    QCOMPARE(album.count(), 2);
    QCOMPARE(album.at(1).at(0).toInt(), 4);
    QCOMPARE(track.count(), 3);

    QTest::mouseDClick(tree->viewport(), Qt::LeftButton, { }, centre(disc, 0)); // a disc row: nothing
    QCOMPARE(track.count(), 3);
    QCOMPARE(album.count(), 2);

    // A right click: the row becomes the selection unless it is part of it already.
    v.selectTracks({ 1, 2 });
    emit tree->customContextMenuRequested(centre(row(v, 2), 1));
    QCOMPARE(menu.count(), 1);
    QCOMPARE(v.selectedTrackIds(), QList<qint64>({ 1, 2 }));
    emit tree->customContextMenuRequested(centre(row(v, 7), 1));
    QCOMPARE(menu.count(), 2);
    QCOMPARE(v.selectedTrackIds(), QList<qint64>({ 7 }));
    emit tree->customContextMenuRequested(centre(row(v, -3), 0));
    QCOMPARE(menu.count(), 3);
    QCOMPARE(v.selectedTrackIds(), QList<qint64>({ 4, 6, 5 }));
    emit tree->customContextMenuRequested(centre(disc, 0));
    QCOMPARE(menu.count(), 3);
    QCOMPARE(tree->contextMenuPolicy(), Qt::CustomContextMenu);
}

void AlbumViewTest::jumpBar()
{
    // AV-BAR
    AlbumTracksView v;
    NEED_TREE(v);
    auto *bar = v.findChild<QWidget *>(s(u"jumpBar"));
    QVERIFY(bar);
    const QString letters = s(u"#ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    QCOMPARE(bar->findChildren<QToolButton *>().size(), 27);
    for (const QChar c : letters) {
        auto *button = bar->findChild<QToolButton *>(s(u"jump_") + c);
        QVERIFY2(button, qPrintable(QString(c)));
        QCOMPARE(button->text(), QString(c));
    }
    v.setTracks(sortedForAlbums(nine()));
    QCOMPARE(v.currentGroup(), -1);
    v.selectTracks({ 7 });
    QSignalSpy changed(&v, &AlbumTracksView::selectionChanged);
    bar->findChild<QToolButton *>(s(u"jump_B"))->click();
    QCOMPARE(v.currentGroup(), 2);
    QCOMPARE(treeOf(v)->currentItem(), treeOf(v)->topLevelItem(2));
    bar->findChild<QToolButton *>(s(u"jump_Z"))->click(); // no such artist: nothing
    QCOMPARE(v.currentGroup(), 2);
    v.jumpTo(QLatin1Char('#'));
    QCOMPARE(v.currentGroup(), 5);
    v.jumpTo(QLatin1Char('a'));
    QCOMPARE(v.currentGroup(), 0);
    QCOMPARE(v.selectedTrackIds(), QList<qint64>({ 7 })); // jumping selects nothing
    QCOMPARE(changed.count(), 0);
    // The current row inside an album.
    treeOf(v)->setCurrentItem(row(v, 2), 0, QItemSelectionModel::NoUpdate);
    QCOMPARE(v.currentGroup(), 3);
}

void AlbumViewTest::inTheShell()
{
    // AV-SHELL
    Rig r;
    if (!r.shell->navigator())
        QFAIL("plan 30's library shell is not there");
    const QStringList sortBefore = r.shell->model()->sortFields();
    AlbumViewing viewing(r.shell.get(), &r.settings);
    NEED_VIEW(viewing);
    TrackListPage *page = r.shell->trackList();
    auto *table = page->findChild<QTreeView *>(s(u"trackView"));
    auto *combo = page->findChild<QComboBox *>(s(u"mainViewCombo"));
    QVERIFY(table && combo);
    QCOMPARE(viewing.mode(), s(u"albums"));
    QCOMPARE(viewing.view()->parentWidget(), page);
    QCOMPARE(combo->count(), 2);
    QCOMPARE(combo->itemText(0), s(u"Album and Tracks"));
    QCOMPARE(combo->itemText(1), s(u"Tracks"));
    QCOMPARE(combo->currentIndex(), 0);
    QVERIFY(table->isHidden());
    QVERIFY(!viewing.view()->isHidden());
    QVERIFY(!page->findChild<QLineEdit *>(s(u"searchBox"))->isHidden());
    QVERIFY(!page->findChild<QLabel *>(s(u"summaryLabel"))->isHidden());
    auto *layout = qobject_cast<QBoxLayout *>(page->layout());
    QVERIFY(layout);
    QCOMPARE(layout->indexOf(combo), 0);
    QCOMPARE(layout->indexOf(viewing.view()), layout->indexOf(table) + 1);

    // The four tracks of the Music section, in the order of the view.
    QCOMPARE(r.shell->nodeTracks().size(), 4);
    QCOMPARE(viewing.view()->groups(), albumGroups(sortedForAlbums(r.shell->nodeTracks())));
    QVERIFY(viewing.view()->groups().size() >= 2);
    QCOMPARE(r.shell->model()->sortFields(), sortBefore); // the plain list's order is not touched
    const auto viewIds = [&viewing] {
        QList<qint64> ids;
        for (const AlbumGroup &g : viewing.view()->groups())
            ids += g.trackIds;
        std::sort(ids.begin(), ids.end());
        return ids;
    };
    const auto modelIds = [&r] {
        QList<qint64> ids;
        for (int i = 0; i < r.shell->model()->rowCount(); ++i)
            ids.append(r.shell->model()->trackId(i));
        std::sort(ids.begin(), ids.end());
        return ids;
    };
    // It follows the search box, the node and the library.
    page->setSearchText(s(u"adele"));
    QVERIFY(modelIds().size() < 4);
    QCOMPARE(viewIds(), modelIds());
    page->setSearchText(QString());
    QCOMPARE(viewIds().size(), 4);
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Inbox")));
    QCOMPARE(viewIds(), modelIds());
    QCOMPARE(viewIds().size(), 1);
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Music")));
    const auto adele = r.library->trackByPath(r.mp3);
    QVERIFY(adele.has_value());
    QCOMPARE(r.library->remove({ adele->id }), 1);
    QCOMPARE(viewIds().size(), 3);
    QCOMPARE(viewIds(), modelIds());
    // A narrowing set by another part is respected.
    const qint64 keepOnly = viewIds().value(0);
    r.shell->setNarrowing([keepOnly](const LibraryTrack &t) { return t.id == keepOnly; });
    QCOMPARE(viewIds(), QList<qint64>({ keepOnly }));
    r.shell->setNarrowing({ });
    QCOMPARE(viewIds().size(), 3);
}

void AlbumViewTest::selectionAndPlay()
{
    // AV-SHELL: the selection is the shell's; activation asks the shell to play.
    Rig r;
    if (!r.shell->navigator())
        QFAIL("plan 30's library shell is not there");
    AlbumViewing viewing(r.shell.get(), &r.settings);
    NEED_VIEW(viewing);
    AlbumTracksView *view = viewing.view();
    const QList<LibraryTrack> shown = sortedForAlbums(r.shell->nodeTracks());
    QCOMPARE(shown.size(), 4);
    QStringList paths;
    for (const LibraryTrack &t : shown)
        paths.append(t.path);

    view->selectTracks({ shown.at(2).id, shown.at(0).id });
    QList<qint64> want { shown.at(0).id, shown.at(2).id };
    QList<qint64> got = r.shell->selectedTrackIds();
    std::sort(want.begin(), want.end());
    std::sort(got.begin(), got.end());
    QCOMPARE(got, want);
    QVERIFY(row(*view, -1));
    view->selectTracks({ });
    row(*view, -1)->setSelected(true); // a heading: the whole album
    got = r.shell->selectedTrackIds();
    want = view->groups().at(0).trackIds;
    std::sort(want.begin(), want.end());
    std::sort(got.begin(), got.end());
    QCOMPARE(got, want);

    QSignalSpy play(r.shell.get(), &LibraryShell::playRequested);
    emit view->trackActivated(shown.at(2).id);
    QCOMPARE(play.count(), 1);
    QCOMPARE(play.at(0).at(0).toStringList(), paths); // everything shown, in the view's order
    QCOMPARE(play.at(0).at(1).toInt(), 2);
    emit view->albumActivated(1);
    QCOMPARE(play.count(), 2);
    QCOMPARE(play.at(1).at(0).toStringList(), view->groups().at(1).paths); // that album only
    QCOMPARE(play.at(1).at(1).toInt(), 0);
    emit view->trackActivated(987654); // not shown
    emit view->albumActivated(99);
    emit view->albumActivated(-1);
    QCOMPARE(play.count(), 2);
    // Narrowed by the search, "everything shown" is less.
    r.shell->trackList()->setSearchText(s(u"adele"));
    QVERIFY(!view->groups().isEmpty());
    const qint64 first = view->groups().at(0).trackIds.value(0);
    int count = 0;
    for (const AlbumGroup &g : view->groups())
        count += int(g.trackIds.size());
    emit view->trackActivated(first);
    QCOMPARE(play.count(), 3);
    QCOMPARE(play.at(2).at(0).toStringList().size(), count);
    QVERIFY(count < 4);
    QCOMPARE(play.at(2).at(1).toInt(), 0);
    r.shell->trackList()->setSearchText(QString());

    // The list's own context menu.
    auto *menu = r.shell->trackList()->findChild<QMenu *>(s(u"trackContextMenu"));
    QVERIFY(menu);
    QVERIFY(!menu->isVisible());
    emit view->menuRequested(QPoint(20, 20));
    QTRY_VERIFY(menu->isVisible());
    menu->hide();
}

void AlbumViewTest::modes()
{
    // AV-SHELL: the two modes and what is stored.
    Rig r;
    if (!r.shell->navigator())
        QFAIL("plan 30's library shell is not there");
    {
        AlbumViewing viewing(r.shell.get(), &r.settings);
        NEED_VIEW(viewing);
        auto *table = r.shell->trackList()->findChild<QTreeView *>(s(u"trackView"));
        auto *combo = r.shell->trackList()->findChild<QComboBox *>(s(u"mainViewCombo"));
        QVERIFY(table && combo);
        QSignalSpy changed(&viewing, &AlbumViewing::modeChanged);
        QSignalSpy play(r.shell.get(), &LibraryShell::playRequested);
        const qint64 some = viewing.view()->groups().value(0).trackIds.value(0);
        viewing.setMode(s(u"tracks"));
        QCOMPARE(viewing.mode(), s(u"tracks"));
        QCOMPARE(changed.count(), 1);
        QVERIFY(!table->isHidden());
        QVERIFY(viewing.view()->isHidden());
        QCOMPARE(combo->currentIndex(), 1);
        QCOMPARE(viewing.view()->groups().size(), 0); // given no tracks
        {
            QSettings later(nextRun(r.settings), QSettings::IniFormat);
            QCOMPARE(later.value(s(u"view/mainPanel")).toString(), s(u"tracks")); // stored at once
        }
        emit viewing.view()->trackActivated(some);
        emit viewing.view()->albumActivated(0);
        QCOMPARE(play.count(), 0); // nothing happens in this mode
        r.shell->trackList()->selectTracks({ some });
        viewing.view()->selectTracks({ });
        QCOMPARE(r.shell->selectedTrackIds(), QList<qint64>({ some })); // the list's selection is its own again
        viewing.setMode(s(u"TRACKS")); // the same
        QCOMPARE(changed.count(), 1);

        combo->setCurrentIndex(0); // by hand
        QCOMPARE(viewing.mode(), s(u"albums"));
        QCOMPARE(changed.count(), 2);
        QVERIFY(table->isHidden());
        QVERIFY(!viewing.view()->isHidden());
        QCOMPARE(viewing.view()->groups(), albumGroups(sortedForAlbums(r.shell->nodeTracks())));
        {
            QSettings later(nextRun(r.settings), QSettings::IniFormat);
            QCOMPARE(later.value(s(u"view/mainPanel")).toString(), s(u"albums"));
        }
        viewing.setMode(s(u"banana")); // anything else means albums: the same
        QCOMPARE(viewing.mode(), s(u"albums"));
        QCOMPARE(changed.count(), 2);
        viewing.setMode(s(u"tracks"));
    }
    // As a later run reads it. Each viewing here adds its own widgets to the page, so each gets a shell of its own.
    for (const auto &[stored, want] : { std::pair { u"tracks", u"tracks" }, std::pair { u"TRACKS", u"tracks" },
             std::pair { u"albums", u"albums" }, std::pair { u"banana", u"albums" }, std::pair { u"", u"albums" },
             std::pair { u"true", u"albums" } }) {
        Rig fresh;
        fresh.settings.setValue(s(u"view/mainPanel"), s(stored));
        QSettings later(nextRun(fresh.settings), QSettings::IniFormat);
        AlbumViewing viewing(fresh.shell.get(), &later);
        QVERIFY2(viewing.mode() == s(want), qPrintable(s(stored)));
    }
}

QTEST_MAIN(AlbumViewTest)
#include "tst_ui_albumview.moc"
