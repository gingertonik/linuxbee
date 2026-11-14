// LOCKED ACCEPTANCE TEST (the album view fills once). Written by the reviewer, not the builder.
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

class AlbumViewFillTest : public QObject {
    Q_OBJECT
private slots:
    void sizing();
    void oneFillForManyChanges();
    void theListsSelectionIsTheViews();
};

void AlbumViewFillTest::sizing()
{
    // AV-VIEW: no column is sized to its contents; that measures every row.
    // helpers of the rig this file shares with the plan's own test
    (void)&nextRun;
    (void)&nine;
    (void)&idsOf;
    (void)&named;
    AlbumTracksView v;
    NEED_TREE(v);
    const QHeaderView *header = treeOf(v)->header();
    QCOMPARE(header->count(), 3);
    for (int column = 0; column < 3; ++column)
        QVERIFY2(header->sectionResizeMode(column) != QHeaderView::ResizeToContents, qPrintable(QString::number(column)));
    QCOMPARE(header->sectionResizeMode(1), QHeaderView::Stretch); // the title takes what is left
    QVERIFY(header->sectionSize(0) > 0);
    QVERIFY(header->sectionSize(2) > 0);
    // AV-VIEW: fills() counts the times the rows were built.
    QCOMPARE(v.fills(), 0);
    v.setTracks({ });
    v.setTracks({ });
    QCOMPARE(v.fills(), 2);
}

void AlbumViewFillTest::oneFillForManyChanges()
{
    // AV-FILL: changes make the view stale; it is filled once, at the next
    // turn of the event loop, or at once when view() is asked.
    Rig r;
    if (!r.shell->navigator())
        QFAIL("plan 30's library shell is not there");
    AlbumViewing viewing(r.shell.get(), &r.settings);
    NEED_VIEW(viewing);
    AlbumTracksView *view = viewing.view();
    QCoreApplication::processEvents();
    const auto shownIds = [view] {
        QList<qint64> ids;
        for (const AlbumGroup &g : view->groups())
            ids += g.trackIds;
        return ordered(ids);
    };
    const auto modelIds = [&r] {
        QList<qint64> ids;
        for (int i = 0; i < r.shell->model()->rowCount(); ++i)
            ids.append(r.shell->model()->trackId(i));
        return ordered(ids);
    };
    QCOMPARE(shownIds().size(), 4);
    int fills = view->fills();
    QVERIFY(fills >= 1);

    // Nothing changed: nothing is filled.
    QCoreApplication::processEvents();
    QCOMPARE(view->fills(), fills);

    // One change of the library: one fill, not two.
    const auto inbox = r.library->trackByPath(r.inboxFlac);
    QVERIFY(inbox.has_value());
    QCOMPARE(r.library->setCategory({ inbox->id }, s(u"Music")), 1);
    QCOMPARE(view->fills(), fills); // not yet
    QTRY_COMPARE(view->fills(), fills + 1);
    QCOMPARE(shownIds(), modelIds());
    QCOMPARE(shownIds().size(), 5);
    QCoreApplication::processEvents();
    QCOMPARE(view->fills(), fills + 1);

    // Many changes in one turn: still one fill.
    fills = view->fills();
    const auto adele = r.library->trackByPath(r.mp3);
    QVERIFY(adele.has_value());
    QCOMPARE(r.library->remove({ adele->id }), 1);
    QCOMPARE(r.library->setCategory({ inbox->id }, s(u"Inbox")), 1);
    r.shell->trackList()->setSearchText(s(u"zzz"));
    r.shell->trackList()->setSearchText(QString());
    const qint64 keepOut = inbox->id;
    r.shell->setNarrowing([keepOut](const LibraryTrack &t) { return t.id != keepOut; });
    r.shell->setNarrowing({ });
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Inbox")));
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Music")));
    QCOMPARE(view->fills(), fills);
    QTRY_COMPARE(view->fills(), fills + 1);
    QCOMPARE(shownIds(), modelIds());
    QCOMPARE(shownIds().size(), 3);

    // Asking for the view brings it up to date at once, and the turn after adds nothing.
    fills = view->fills();
    r.shell->trackList()->setSearchText(s(u"zzz"));
    QCOMPARE(view->fills(), fills);
    QCOMPARE(viewing.view()->fills(), fills + 1);
    QCOMPARE(shownIds().size(), 0);
    QCoreApplication::processEvents();
    QCOMPARE(view->fills(), fills + 1);
    r.shell->trackList()->setSearchText(QString());
    QCOMPARE(viewing.view()->groups(), albumGroups(sortedForAlbums(r.shell->nodeTracks())));

    // In mode `tracks` the view is emptied and then left alone.
    viewing.setMode(s(u"tracks"));
    QCoreApplication::processEvents();
    QCOMPARE(shownIds().size(), 0);
    fills = view->fills();
    QCOMPARE(r.library->setCategory({ inbox->id }, s(u"Music")), 1);
    r.shell->trackList()->setSearchText(s(u"a"));
    r.shell->trackList()->setSearchText(QString());
    QCoreApplication::processEvents();
    QCOMPARE(viewing.view()->fills(), fills);
    // Back in mode `albums` it is filled at once.
    viewing.setMode(s(u"albums"));
    QCOMPARE(view->fills(), fills + 1);
    QCOMPARE(shownIds(), modelIds());
    QCOMPARE(shownIds().size(), 4);
}

void AlbumViewFillTest::theListsSelectionIsTheViews()
{
    // AV-SHELL: in mode `albums` the selection is one, whichever side set it.
    Rig r;
    if (!r.shell->navigator())
        QFAIL("plan 30's library shell is not there");
    AlbumViewing viewing(r.shell.get(), &r.settings);
    NEED_VIEW(viewing);
    TrackListPage *page = r.shell->trackList();
    const QList<LibraryTrack> shown = sortedForAlbums(r.shell->nodeTracks());
    QCOMPARE(shown.size(), 4);
    const QList<qint64> two = ordered({ shown.at(0).id, shown.at(3).id });
    QList<qint64> all;
    for (const LibraryTrack &t : shown)
        all.append(t.id);
    all = ordered(all);

    // The list's selection changes (as Select All does): the view shows it.
    page->selectTracks(all);
    QCOMPARE(ordered(viewing.view()->selectedTrackIds()), all);
    QCOMPARE(ordered(r.shell->selectedTrackIds()), all);
    page->selectTracks(two);
    QCOMPARE(ordered(viewing.view()->selectedTrackIds()), two);
    page->selectTracks({ });
    QCOMPARE(viewing.view()->selectedTrackIds(), QList<qint64>());

    // A selected heading stays a selected heading when the list follows it.
    QVERIFY(row(*viewing.view(), -1));
    row(*viewing.view(), -1)->setSelected(true);
    QVERIFY(row(*viewing.view(), -1)->isSelected());
    QCOMPARE(ordered(r.shell->selectedTrackIds()), ordered(viewing.view()->groups().at(0).trackIds));
    viewing.view()->selectTracks({ });

    // A selection made in mode `tracks` is the view's after the switch.
    viewing.setMode(s(u"tracks"));
    page->selectTracks(two);
    QCOMPARE(ordered(r.shell->selectedTrackIds()), two);
    viewing.setMode(s(u"albums"));
    QCOMPARE(ordered(viewing.view()->selectedTrackIds()), two);
    QCOMPARE(ordered(r.shell->selectedTrackIds()), two);
    // and one made in the view is the list's after switching back.
    viewing.view()->selectTracks({ shown.at(1).id });
    viewing.setMode(s(u"tracks"));
    QCOMPARE(r.shell->selectedTrackIds(), QList<qint64>({ shown.at(1).id }));
}

QTEST_MAIN(AlbumViewFillTest)
#include "tst_ui_albumview_fill.moc"
