// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/artist-browser.md (AB-x). Runs on
// the offscreen platform. The last tests use a real library (in memory),
// the real window and the real library shell.
#include <QComboBox>
#include <QFile>
#include <QLineEdit>
#include <QListWidget>
#include <QSettings>
#include <QSignalSpy>
#include <QSplitter>
#include <QTemporaryDir>
#include <QtTest>

#include "actions/actionregistry.h"
#include "browser/artistbrowser.h"
#include "library/artistindex.h"
#include "mainwindow/mainwindow.h"
#include "navigator/navigatorpanel.h"
#include "shell/artistbrowsing.h"
#include "shell/libraryshell.h"
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

LibraryTrack track(qint64 id, const char16_t *albumArtist, const char16_t *artist, const char16_t *album)
{
    LibraryTrack t;
    t.id = id;
    t.path = QStringLiteral("/music/%1.flac").arg(id);
    if (*albumArtist)
        t.tags.fields.insert(s(u"Album Artist"), { s(albumArtist) });
    if (*artist)
        t.tags.fields.insert(s(u"Artist"), { s(artist) });
    if (*album)
        t.tags.fields.insert(s(u"Album"), { s(album) });
    t.tags.fields.insert(s(u"Title"), { QStringLiteral("Track %1").arg(id) });
    return t;
}

// Nine tracks, five artists and two tracks with none.
QList<LibraryTrack> nine()
{
    return {
        track(1, u"The Beatles", u"The Beatles", u"Abbey Road"),
        track(2, u"the beatles", u"George Harrison", u"Abbey Road"),
        track(3, u"The Beatles", u"The Beatles", u"Revolver"),
        track(4, u"", u"Bj\u00f6rk", u"Homogenic"),
        track(5, u"Various Artists", u"Adele", u"Hits"),
        track(6, u"", u"adele", u""),
        track(7, u"", u"Adele", u""),
        track(8, u"", u"", u"Unknown Things"),
        track(9, u"  ", u" ", u""),
    };
}

// Four tracks for browsing by genre and by year.
LibraryTrack tagged(qint64 id, const char16_t *albumArtist, const char16_t *album, const QStringList &genres, const char16_t *year)
{
    LibraryTrack t = track(id, albumArtist, u"", album);
    if (!genres.isEmpty())
        t.tags.fields.insert(s(u"Genre"), genres);
    if (*year)
        t.tags.fields.insert(s(u"Year"), { s(year) });
    return t;
}

QList<LibraryTrack> four()
{
    return {
        tagged(1, u"The Beatles", u"Abbey Road", { s(u"Rock"), s(u" Pop "), s(u"rock"), s(u"  ") }, u"1969-09-26"),
        tagged(2, u"The Beatles", u"Let It Be", { s(u"pop") }, u" 1969 "),
        tagged(3, u"Adele", u"21", { }, u""),
        tagged(4, u"Miles Davis", u"Tutu", { s(u"Jazz") }, u"2001"),
    };
}

template <typename T> T *need(QWidget &in, const char16_t *name)
{
    if (T *found = in.findChild<T *>(s(name)))
        return found;
    QTest::qFail(qPrintable(QStringLiteral("no widget named ") + s(name)), __FILE__, __LINE__);
    static T standIn;
    return &standIn;
}

QStringList rows(ArtistBrowser &b, bool visibleOnly = false)
{
    QStringList out;
    auto *list = b.findChild<QListWidget *>(s(u"artistList"));
    for (int i = 0; list && i < list->count(); ++i)
        if (!visibleOnly || !list->item(i)->isHidden())
            out.append(list->item(i)->text());
    return out;
}

QString currentRow(ArtistBrowser &b)
{
    auto *list = b.findChild<QListWidget *>(s(u"artistList"));
    return list && list->currentItem() ? list->currentItem()->text() : QStringLiteral("<none>");
}

bool clickRow(ArtistBrowser &b, const QString &text)
{
    auto *list = b.findChild<QListWidget *>(s(u"artistList"));
    for (int i = 0; list && i < list->count(); ++i)
        if (list->item(i)->text() == text) {
            list->setCurrentRow(i);
            return true;
        }
    return false;
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

#define NEED_BROWSER(browsing) \
    do { \
        if (!(browsing).browser() || !(browsing).browser()->findChild<QListWidget *>(s(u"artistList"))) \
            QFAIL("the artist browser has not been built"); \
    } while (false)

}

class ArtistBrowserTest : public QObject {
    Q_OBJECT
private slots:
    void artistOfATrack();
    void sameArtists();
    void index();
    void listRows();
    void choice();
    void choiceSurvivesNewTracks();
    void filter();
    void inTheShell();
    void followsTheNode();
    void shownAndHidden();
    void fields();
    void byField();
    void fieldInTheBrowser();
    void fieldStored();
};

void ArtistBrowserTest::artistOfATrack()
{
    // AB-ARTIST
    QCOMPARE(browseArtist(track(1, u"The Beatles", u"George Harrison", u"x")), s(u"The Beatles"));
    QCOMPARE(browseArtist(track(1, u"", u"Bj\u00f6rk", u"x")), s(u"Bj\u00f6rk"));
    QCOMPARE(browseArtist(track(1, u"   ", u"  Adele ", u"x")), s(u"Adele")); // blank is not a value; trimmed
    QCOMPARE(browseArtist(track(1, u"", u"", u"x")), QString());
    LibraryTrack several = track(1, u"", u"", u"x");
    several.tags.fields.insert(s(u"Artist"), { s(u" "), s(u"Second"), s(u"Third") });
    QCOMPARE(browseArtist(several), s(u"Second")); // the first that is not blank
    several.tags.fields.insert(s(u"Album Artist"), { s(u"One"), s(u"Two") });
    QCOMPARE(browseArtist(several), s(u"One"));
}

void ArtistBrowserTest::sameArtists()
{
    // AB-SAME
    QVERIFY(sameArtist(s(u"The Beatles"), s(u"the beatles")));
    QVERIFY(sameArtist(s(u" Adele"), s(u"ADELE  ")));
    QVERIFY(sameArtist(QString(), s(u"  ")));
    QVERIFY(!sameArtist(s(u"Bjork"), s(u"Bj\u00f6rk")));
    QVERIFY(!sameArtist(s(u"Beatles"), s(u"The Beatles")));
    QVERIFY(!sameArtist(s(u"Adele"), QString()));
}

void ArtistBrowserTest::index()
{
    // AB-INDEX
    const QList<ArtistEntry> got = artistIndex(nine());
    const QList<ArtistEntry> want {
        { s(u"adele"), 2, 1 }, // tracks 6 and 7: first met as "adele"; no album, which counts as one
        { s(u"The Beatles"), 3, 2 }, // "The" is ignored in the order; Abbey Road and Revolver
        { s(u"Bj\u00f6rk"), 1, 1 },
        { s(u"Various Artists"), 1, 1 }, // track 5 is filed under its album artist, not under Adele
        { QString(), 2, 2 }, // tracks 8 and 9: last; one album name and "no album"
    };
    QCOMPARE(got.size(), want.size());
    for (int i = 0; i < want.size(); ++i) {
        QCOMPARE(got.at(i).name, want.at(i).name);
        QCOMPARE(got.at(i).tracks, want.at(i).tracks);
        QCOMPARE(got.at(i).albums, want.at(i).albums);
    }
    QCOMPARE(artistIndex({ }), QList<ArtistEntry>());
    // The ignore words are the caller's: with none, "The Beatles" sorts under T.
    SortOptions plain;
    plain.ignoreWords.clear();
    const QList<ArtistEntry> strict = artistIndex(nine(), plain);
    QCOMPARE(strict.at(0).name, s(u"adele"));
    QCOMPARE(strict.at(1).name, s(u"Bj\u00f6rk"));
    QCOMPARE(strict.at(2).name, s(u"The Beatles"));
    // The spelling is that of the first track met, in the order given.
    QList<LibraryTrack> reversed = nine();
    std::reverse(reversed.begin(), reversed.end());
    QCOMPARE(artistIndex(reversed).at(0).name, s(u"Adele"));
}

void ArtistBrowserTest::listRows()
{
    // AB-LIST
    ArtistBrowser b;
    if (!b.findChild<QListWidget *>(s(u"artistList")) || !b.findChild<QLineEdit *>(s(u"artistFilterEdit")))
        QFAIL("the browser has not built its widgets");
    QCOMPARE(rows(b), QStringList({ s(u"All Artists (0)") }));
    QCOMPARE(b.artists(), QStringList());
    QVERIFY(!b.hasChoice());
    QVERIFY(need<QLineEdit>(b, u"artistFilterEdit")->isClearButtonEnabled());
    b.setTracks(nine());
    QCOMPARE(rows(b), QStringList({ s(u"All Artists (5)"), s(u"adele (2)"), s(u"The Beatles (3)"), s(u"Bj\u00f6rk (1)"),
                          s(u"Various Artists (1)"), s(u"[Unknown Artist] (2)") }));
    QCOMPARE(b.artists(), QStringList({ s(u"adele"), s(u"The Beatles"), s(u"Bj\u00f6rk"), s(u"Various Artists"), QString() }));
    QCOMPARE(currentRow(b), s(u"All Artists (5)"));
    QVERIFY(!b.hasChoice());
    for (const LibraryTrack &t : nine())
        QVERIFY(b.keeps(t)); // no choice: everything
}

void ArtistBrowserTest::choice()
{
    // AB-CHOICE
    ArtistBrowser b;
    b.setTracks(nine());
    if (rows(b).size() != 6)
        QFAIL("the list is not filled");
    QSignalSpy changed(&b, &ArtistBrowser::choiceChanged);
    QVERIFY(clickRow(b, s(u"The Beatles (3)")));
    QCOMPARE(changed.size(), 1);
    QVERIFY(b.hasChoice());
    QCOMPARE(b.currentArtist(), s(u"The Beatles"));
    QList<qint64> kept;
    for (const LibraryTrack &t : nine())
        if (b.keeps(t))
            kept.append(t.id);
    QCOMPARE(kept, QList<qint64>({ 1, 2, 3 })); // track 2 by its album artist, whatever its case
    QVERIFY(clickRow(b, s(u"The Beatles (3)"))); // the same again: no signal
    QCOMPARE(changed.size(), 1);
    b.chooseArtist(s(u"THE BEATLES"));
    QCOMPARE(changed.size(), 1);

    b.chooseArtist(s(u"ADELE"));
    QCOMPARE(changed.size(), 2);
    QCOMPARE(b.currentArtist(), s(u"adele")); // as the list spells it
    QCOMPARE(currentRow(b), s(u"adele (2)"));
    kept.clear();
    for (const LibraryTrack &t : nine())
        if (b.keeps(t))
            kept.append(t.id);
    QCOMPARE(kept, QList<qint64>({ 6, 7 })); // not track 5, which is filed under Various Artists

    // The tracks with no artist can be chosen too.
    QVERIFY(clickRow(b, s(u"[Unknown Artist] (2)")));
    QCOMPARE(changed.size(), 3);
    QVERIFY(b.hasChoice());
    QCOMPARE(b.currentArtist(), QString());
    kept.clear();
    for (const LibraryTrack &t : nine())
        if (b.keeps(t))
            kept.append(t.id);
    QCOMPARE(kept, QList<qint64>({ 8, 9 }));

    QVERIFY(clickRow(b, s(u"All Artists (5)")));
    QCOMPARE(changed.size(), 4);
    QVERIFY(!b.hasChoice());
    b.chooseAll(); // already all
    QCOMPARE(changed.size(), 4);
    b.chooseArtist(s(u"Nobody Here")); // not held: all, which it is already
    QCOMPARE(changed.size(), 4);
    b.chooseArtist(s(u"Bj\u00f6rk"));
    b.chooseArtist(s(u"Nobody Here")); // not held: back to all
    QCOMPARE(changed.size(), 6);
    QVERIFY(!b.hasChoice());
    QCOMPARE(currentRow(b), s(u"All Artists (5)"));
}

void ArtistBrowserTest::choiceSurvivesNewTracks()
{
    // AB-CHOICE: setTracks keeps the choice when it can.
    ArtistBrowser b;
    b.setTracks(nine());
    if (rows(b).size() != 6)
        QFAIL("the list is not filled");
    b.chooseArtist(s(u"The Beatles"));
    QSignalSpy changed(&b, &ArtistBrowser::choiceChanged);
    QList<LibraryTrack> fewer = nine().mid(2); // one Beatles track left, spelt as before
    b.setTracks(fewer);
    QCOMPARE(changed.size(), 0);
    QVERIFY(b.hasChoice());
    QCOMPARE(currentRow(b), s(u"The Beatles (1)"));
    b.setTracks(nine().mid(3)); // none left
    QCOMPARE(changed.size(), 1);
    QVERIFY(!b.hasChoice());
    QCOMPARE(currentRow(b), s(u"All Artists (4)"));
    b.setTracks(nine()); // filling again is not a change of choice
    QCOMPARE(changed.size(), 1);
}

void ArtistBrowserTest::filter()
{
    // AB-FILTER
    ArtistBrowser b;
    b.setTracks(nine());
    if (rows(b).size() != 6)
        QFAIL("the list is not filled");
    auto *edit = need<QLineEdit>(b, u"artistFilterEdit");
    QSignalSpy changed(&b, &ArtistBrowser::choiceChanged);
    edit->setText(s(u"BJORK")); // case and accents ignored
    QCOMPARE(rows(b, true), QStringList({ s(u"All Artists (5)"), s(u"Bj\u00f6rk (1)") }));
    QCOMPARE(b.artists().size(), 5); // hidden, not gone
    edit->setText(s(u"e"));
    QCOMPARE(rows(b, true), QStringList({ s(u"All Artists (5)"), s(u"adele (2)"), s(u"The Beatles (3)") }));
    QCOMPARE(changed.size(), 0); // filtering is not choosing
    // The chosen artist stays visible whatever the filter says.
    b.chooseArtist(s(u"Various Artists"));
    edit->setText(s(u"zzz"));
    QCOMPARE(rows(b, true), QStringList({ s(u"All Artists (5)"), s(u"Various Artists (1)") }));
    QVERIFY(b.hasChoice());
    QCOMPARE(currentRow(b), s(u"Various Artists (1)"));
    QCOMPARE(changed.size(), 1);
    edit->clear();
    QCOMPARE(rows(b, true).size(), 6);
    QCOMPARE(currentRow(b), s(u"Various Artists (1)"));
    QCOMPARE(changed.size(), 1);
}

void ArtistBrowserTest::inTheShell()
{
    // AB-SHELL
    Rig r;
    if (!r.shell->navigator())
        QFAIL("plan 30's library shell is not there");
    ArtistBrowsing browsing(r.shell.get(), &r.settings);
    QVERIFY(browsing.isShown());
    NEED_BROWSER(browsing);
    QCOMPARE(r.shell->browser(), browsing.browser());
    auto *split = r.window.findChild<QSplitter *>(s(u"browserSplitter"));
    QVERIFY(split);
    QCOMPARE(split->widget(0), browsing.browser());
    // The Music section's four tracks: three album artists, and the Ogg file by its artist.
    QStringList want;
    for (const ArtistEntry &e : artistIndex(r.shell->nodeTracks()))
        want.append(e.name);
    QCOMPARE(want.size(), 4);
    QCOMPARE(browsing.browser()->artists(), want);
    QVERIFY(want.contains(s(u"Adele")));
    QVERIFY(want.contains(s(u"Various Artists")));
    QVERIFY(want.contains(s(u"M4A Album Artist")));
    QCOMPARE(r.shown().size(), 4);

    browsing.browser()->chooseArtist(s(u"Adele"));
    QCOMPARE(r.shown(), QStringList({ r.mp3 })); // the list is narrowed
    QCOMPARE(r.shell->nodeTracks().size(), 4); // the node is not
    QVERIFY(clickRow(*browsing.browser(), s(u"Various Artists (1)")));
    QCOMPARE(r.shown(), QStringList({ r.flac }));
    // The search box narrows within the artist.
    r.shell->trackList()->setSearchText(s(u"adele"));
    QCOMPARE(r.shown(), QStringList());
    r.shell->trackList()->setSearchText(QString());
    browsing.browser()->chooseAll();
    QCOMPARE(r.shown().size(), 4);
}

void ArtistBrowserTest::followsTheNode()
{
    // AB-SHELL: the browser shows the artists of whatever node is chosen, and of the library as it changes.
    Rig r;
    if (!r.shell->navigator())
        QFAIL("plan 30's library shell is not there");
    ArtistBrowsing browsing(r.shell.get(), &r.settings);
    NEED_BROWSER(browsing);
    browsing.browser()->chooseArtist(s(u"Various Artists"));
    QCOMPARE(r.shown(), QStringList({ r.flac }));
    // The Inbox holds one track, by the same album artist: the choice is kept.
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Inbox")));
    QCOMPARE(browsing.browser()->artists(), QStringList({ s(u"Various Artists") }));
    QVERIFY(browsing.browser()->hasChoice());
    QCOMPARE(r.shown(), QStringList({ r.inboxFlac }));
    // Back in Music with Adele chosen, then to the Inbox, where there is no Adele: all.
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Music")));
    browsing.browser()->chooseArtist(s(u"Adele"));
    QCOMPARE(r.shown(), QStringList({ r.mp3 }));
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Inbox")));
    QVERIFY(!browsing.browser()->hasChoice());
    QCOMPARE(r.shown(), QStringList({ r.inboxFlac })); // not an empty list narrowed to an artist who is not here
    // The library changes: the artist list follows.
    QVERIFY(r.shell->navigator()->selectNode(s(u"library:Music")));
    QCOMPARE(browsing.browser()->artists().size(), 4);
    const auto adele = r.library->trackByPath(r.mp3);
    QVERIFY(adele.has_value());
    QCOMPARE(r.library->remove({ adele->id }), 1);
    QCOMPARE(browsing.browser()->artists().size(), 3);
    QVERIFY(!browsing.browser()->artists().contains(s(u"Adele")));
}

void ArtistBrowserTest::shownAndHidden()
{
    // AB-SHELL: showing, hiding, and what is stored.
    Rig r;
    if (!r.shell->navigator())
        QFAIL("plan 30's library shell is not there");
    {
        ArtistBrowsing browsing(r.shell.get(), &r.settings);
        NEED_BROWSER(browsing);
        browsing.browser()->chooseArtist(s(u"Adele"));
        QCOMPARE(r.shown(), QStringList({ r.mp3 }));
        browsing.setShown(false);
        QVERIFY(!browsing.isShown());
        QVERIFY(browsing.browser() == nullptr);
        QVERIFY(r.shell->browser() == nullptr);
        QCOMPARE(r.shown().size(), 4); // the narrowing went with it
        QSettings later(nextRun(r.settings), QSettings::IniFormat);
        QCOMPARE(later.value(s(u"browser/artistsShown")).toString(), s(u"false")); // stored at once
        browsing.setShown(true);
        NEED_BROWSER(browsing);
        QVERIFY(!browsing.browser()->hasChoice()); // a new one, with no choice
        QCOMPARE(r.shell->browser(), browsing.browser());
        QCOMPARE(browsing.browser()->artists().size(), 4);
        QCOMPARE(r.settings.value(s(u"browser/artistsShown")).toString(), s(u"true"));
        browsing.setShown(false);
    }
    // As a later run reads it: hidden.
    QSettings later(nextRun(r.settings), QSettings::IniFormat);
    {
        ArtistBrowsing hidden(r.shell.get(), &later);
        QVERIFY(!hidden.isShown());
        QVERIFY(hidden.browser() == nullptr);
        QVERIFY(r.shell->browser() == nullptr);
    }
    // Anything but "false" and "0" means shown.
    for (const char16_t *text : { u"perhaps", u"true", u"1", u"" }) {
        later.setValue(s(u"browser/artistsShown"), s(text));
        ArtistBrowsing shown(r.shell.get(), &later);
        QVERIFY2(shown.isShown(), qPrintable(s(text)));
    }
    for (const char16_t *text : { u"FALSE", u"0" }) {
        later.setValue(s(u"browser/artistsShown"), s(text));
        ArtistBrowsing hidden(r.shell.get(), &later);
        QVERIFY2(!hidden.isShown(), qPrintable(s(text)));
    }
}

void ArtistBrowserTest::fields()
{
    // AB-FIELD
    QCOMPARE(browseFields(), QStringList({ s(u"Album Artist"), s(u"Artist"), s(u"Album"), s(u"Composer"), s(u"Genre"),
                                 s(u"Grouping"), s(u"Year") }));
    LibraryTrack t = track(1, u"The Beatles", u"George Harrison", u"Abbey Road");
    t.tags.fields.insert(s(u"Genre"), { s(u"Rock"), s(u" Pop "), s(u"rock"), s(u"  ") });
    t.tags.fields.insert(s(u"Year"), { s(u"1969-09-26"), s(u"2009") });
    QCOMPARE(browseValues(t, s(u"Album Artist")), QStringList({ s(u"The Beatles") }));
    QCOMPARE(browseValues(t, s(u"Artist")), QStringList({ s(u"George Harrison") }));
    QCOMPARE(browseValues(t, s(u"Album")), QStringList({ s(u"Abbey Road") }));
    QCOMPARE(browseValues(t, s(u"Genre")), QStringList({ s(u"Rock"), s(u"Pop") })); // trimmed, the repeat and the blank left out
    QCOMPARE(browseValues(t, s(u"Year")), QStringList({ s(u"1969") })); // the first value's four digits
    QCOMPARE(browseValues(t, s(u"Composer")), QStringList({ QString() })); // nothing: one empty text
    QCOMPARE(browseValues(t, s(u"Grouping")), QStringList({ QString() }));
    QCOMPARE(browseValues(t, s(u"Mood")), QStringList({ s(u"The Beatles") })); // not offered: as Album Artist
    QCOMPARE(browseValues(t, QString()), QStringList({ s(u"The Beatles") }));
    // Album Artist falls back to Artist (AB-ARTIST); Artist alone does not fall back the other way.
    const LibraryTrack onlyAlbumArtist = track(2, u"Various Artists", u"", u"Hits");
    QCOMPARE(browseValues(onlyAlbumArtist, s(u"Artist")), QStringList({ QString() }));
    QCOMPARE(browseValues(track(3, u"", u"Adele", u"21"), s(u"Album Artist")), QStringList({ s(u"Adele") }));
    // Years that are not four digits first are kept whole.
    for (const auto &[stored, want] : { std::pair { u" 1984 ", u"1984" }, std::pair { u"c. 1970", u"c. 1970" },
             std::pair { u"198", u"198" }, std::pair { u"19845", u"1984" }, std::pair { u"  ", u"" } }) {
        LibraryTrack y = track(4, u"x", u"x", u"x");
        y.tags.fields.insert(s(u"Year"), { s(stored) });
        QCOMPARE(browseValues(y, s(u"Year")), QStringList({ s(want) }));
    }
    for (const LibraryTrack &any : nine())
        for (const QString &field : browseFields())
            QVERIFY(!browseValues(any, field).isEmpty());
}

void ArtistBrowserTest::byField()
{
    // AB-INDEX with a field.
    const QList<ArtistEntry> genres = artistIndex(four(), { }, s(u"Genre"));
    QCOMPARE(genres.size(), 4);
    QCOMPARE(genres.value(0), (ArtistEntry { s(u"Jazz"), 1, 1 }));
    QCOMPARE(genres.value(1), (ArtistEntry { s(u"Pop"), 2, 2 })); // spelt as first met; two albums
    QCOMPARE(genres.value(2), (ArtistEntry { s(u"Rock"), 1, 1 })); // "Rock" and "rock" on one track count once
    QCOMPARE(genres.value(3), (ArtistEntry { QString(), 1, 1 })); // no genre: last
    const QList<ArtistEntry> years = artistIndex(four(), { }, s(u"Year"));
    QCOMPARE(years.size(), 3);
    QCOMPARE(years.value(0), (ArtistEntry { s(u"1969"), 2, 2 }));
    QCOMPARE(years.value(1), (ArtistEntry { s(u"2001"), 1, 1 }));
    QCOMPARE(years.value(2), (ArtistEntry { QString(), 1, 1 }));
    // Not given, or not offered: Album Artist.
    QVERIFY(!artistIndex(nine()).isEmpty());
    QCOMPARE(artistIndex(nine(), { }, s(u"Album Artist")), artistIndex(nine()));
    QCOMPARE(artistIndex(nine(), { }, s(u"Mood")), artistIndex(nine()));
    // By Artist the nine fall differently: George Harrison has a row of his own.
    QStringList names;
    for (const ArtistEntry &e : artistIndex(nine(), { }, s(u"Artist")))
        names.append(e.name);
    QCOMPARE(names, QStringList({ s(u"Adele"), s(u"The Beatles"), s(u"Bj\u00f6rk"), s(u"George Harrison"), QString() }));
}

void ArtistBrowserTest::fieldInTheBrowser()
{
    // AB-BYFIELD
    ArtistBrowser b;
    auto *combo = b.findChild<QComboBox *>(s(u"browseFieldCombo"));
    if (!combo || !b.findChild<QListWidget *>(s(u"artistList")))
        QFAIL("the browser has not built its widgets");
    QStringList offered;
    for (int i = 0; i < combo->count(); ++i)
        offered.append(combo->itemText(i));
    QCOMPARE(offered, browseFields());
    QCOMPARE(b.field(), s(u"Album Artist"));
    QCOMPARE(combo->currentText(), s(u"Album Artist"));
    const QList<LibraryTrack> tracks = four();
    b.setTracks(tracks);
    QSignalSpy field(&b, &ArtistBrowser::fieldChanged);
    QSignalSpy choice(&b, &ArtistBrowser::choiceChanged);
    b.chooseArtist(s(u"The Beatles"));
    QCOMPARE(choice.count(), 1);
    need<QLineEdit>(b, u"artistFilterEdit")->setText(s(u"beat"));

    b.setField(s(u"Genre"));
    QCOMPARE(field.count(), 1);
    QCOMPARE(choice.count(), 2); // the choice ended
    QVERIFY(!b.hasChoice());
    QCOMPARE(b.field(), s(u"Genre"));
    QCOMPARE(combo->currentText(), s(u"Genre"));
    QCOMPARE(need<QLineEdit>(b, u"artistFilterEdit")->text(), QString()); // the filter is cleared
    QCOMPARE(rows(b, true),
        QStringList({ s(u"All Genres (4)"), s(u"Jazz (1)"), s(u"Pop (2)"), s(u"Rock (1)"), s(u"[Unknown Genre] (1)") }));
    QCOMPARE(b.artists(), QStringList({ s(u"Jazz"), s(u"Pop"), s(u"Rock"), QString() }));
    b.setField(s(u"Genre")); // the same again: nothing
    QCOMPARE(field.count(), 1);
    QCOMPARE(choice.count(), 2);

    b.chooseArtist(s(u"pop"));
    QCOMPARE(choice.count(), 3);
    QCOMPARE(b.currentArtist(), s(u"Pop"));
    QCOMPARE(currentRow(b), s(u"Pop (2)"));
    QVERIFY(b.keeps(tracks.at(0))); // Rock and Pop: any of its values will do
    QVERIFY(b.keeps(tracks.at(1)));
    QVERIFY(!b.keeps(tracks.at(2)));
    QVERIFY(!b.keeps(tracks.at(3)));
    QVERIFY(clickRow(b, s(u"[Unknown Genre] (1)")));
    QVERIFY(!b.keeps(tracks.at(0)) && !b.keeps(tracks.at(1)) && b.keeps(tracks.at(2)) && !b.keeps(tracks.at(3)));
    QCOMPARE(choice.count(), 4);

    // By hand, in the combo box.
    combo->setCurrentIndex(combo->findText(s(u"Year")));
    QCOMPARE(b.field(), s(u"Year"));
    QCOMPARE(field.count(), 2);
    QCOMPARE(choice.count(), 5);
    QCOMPARE(rows(b), QStringList({ s(u"All Years (3)"), s(u"1969 (2)"), s(u"2001 (1)"), s(u"[Unknown Year] (1)") }));
    // The other fixed texts.
    for (const auto &[name, all] : { std::pair { u"Artist", u"All Artists (1)" }, std::pair { u"Album", u"All Albums (4)" },
             std::pair { u"Composer", u"All Composers (1)" }, std::pair { u"Grouping", u"All Groupings (1)" } }) {
        b.setField(s(name));
        QCOMPARE(rows(b).value(0), s(all));
    }
    QCOMPARE(rows(b).value(1), s(u"[Unknown Grouping] (4)"));
    b.setField(s(u"Composer"));
    QCOMPARE(rows(b).value(1), s(u"[Unknown Composer] (4)"));
    b.setField(s(u"Album"));
    QCOMPARE(rows(b).value(1), s(u"21 (1)"));
    // A name that is not offered means Album Artist.
    const int before = int(field.count());
    b.setField(s(u"Mood"));
    QCOMPARE(b.field(), s(u"Album Artist"));
    QCOMPARE(combo->currentText(), s(u"Album Artist"));
    QCOMPARE(int(field.count()), before + 1);
    QCOMPARE(rows(b), QStringList({ s(u"All Artists (3)"), s(u"Adele (1)"), s(u"The Beatles (2)"), s(u"Miles Davis (1)") }));
    b.setField(s(u"Mood")); // already Album Artist
    QCOMPARE(int(field.count()), before + 1);
}

void ArtistBrowserTest::fieldStored()
{
    // AB-SHELL: the tag is stored at once and read by the next browser.
    Rig r;
    if (!r.shell->navigator())
        QFAIL("plan 30's library shell is not there");
    const auto filedUnder = [&r](const QString &field, const QString &value) {
        QStringList paths;
        for (const LibraryTrack &t : r.shell->nodeTracks())
            for (const QString &v : browseValues(t, field))
                if (sameArtist(v, value) && !paths.contains(t.path))
                    paths.append(t.path);
        paths.sort();
        return paths;
    };
    {
        ArtistBrowsing browsing(r.shell.get(), &r.settings);
        NEED_BROWSER(browsing);
        QCOMPARE(browsing.browser()->field(), s(u"Album Artist"));
        browsing.browser()->chooseArtist(s(u"Adele"));
        QCOMPARE(r.shown(), QStringList({ r.mp3 }));
        browsing.browser()->setField(s(u"Artist"));
        QCOMPARE(r.shown().size(), 4); // the choice ended, and the narrowing with it
        QSettings later(nextRun(r.settings), QSettings::IniFormat);
        QCOMPARE(later.value(s(u"browser/field")).toString(), s(u"Artist")); // stored at once
        // Narrowing by the new tag.
        const QStringList names = browsing.browser()->artists();
        QVERIFY(names.size() >= 2);
        for (const QString &name : names) {
            browsing.browser()->chooseArtist(name);
            QVERIFY(!filedUnder(s(u"Artist"), name).isEmpty());
            QCOMPARE(r.shown(), filedUnder(s(u"Artist"), name));
        }
        // Hidden and shown again: the new browser has the stored tag.
        browsing.setShown(false);
        browsing.setShown(true);
        NEED_BROWSER(browsing);
        QCOMPARE(browsing.browser()->field(), s(u"Artist"));
        QCOMPARE(browsing.browser()->artists(), names);
    }
    QSettings later(nextRun(r.settings), QSettings::IniFormat);
    {
        ArtistBrowsing next(r.shell.get(), &later);
        NEED_BROWSER(next);
        QCOMPARE(next.browser()->field(), s(u"Artist"));
    }
    for (const char16_t *text : { u"Mood", u"", u"artist", u"true" }) { // none of these is offered
        later.setValue(s(u"browser/field"), s(text));
        ArtistBrowsing odd(r.shell.get(), &later);
        NEED_BROWSER(odd);
        QVERIFY2(odd.browser()->field() == s(u"Album Artist"), qPrintable(s(text)));
    }
}

QTEST_MAIN(ArtistBrowserTest)
#include "tst_ui_artistbrowser.moc"
