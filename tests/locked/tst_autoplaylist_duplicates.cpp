// LOCKED ACCEPTANCE TEST (auto-playlists: a duplicate is the same artist and title). Written by
// the reviewer, not the builder. The builder makes these pass by changing src/, never by editing
// this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Rules: docs/spec/auto-playlists.md, A-DUP.
//   W-AP  MusicBee wiki, "Playlists": "filter out duplicates when the same track is repeated on
//         multiple albums"; files that "have the same artist and title"
//   A-x   a linuxBee decision in auto-playlists.md (the public pages are silent)
#include <QtTest>

#include "playlists/autoplaylist.h"

using linuxbee::AutoPlaylist;
using linuxbee::LibraryTrack;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

LibraryTrack make(
    qint64 id, QStringList artists, const char16_t *title, const char16_t *album, int addedDay)
{
    LibraryTrack t;
    t.id = id;
    t.path = QStringLiteral("/m/%1.mp3").arg(id);
    t.category = s(u"Music");
    t.tags.fields.insert(s(u"Artist"), artists);
    t.tags.fields.insert(s(u"Album Artist"), { QStringLiteral("Someone %1").arg(id) });
    if (title)
        t.tags.fields.insert(s(u"Title"), { s(title) });
    t.tags.fields.insert(s(u"Album"), { s(album) });
    t.dateAdded = QDateTime(QDate(2026, 11, addedDay), QTime(12, 0), QTimeZone::UTC);
    return t;
}

QList<qint64> run(const QList<LibraryTrack> &tracks,
    AutoPlaylist::SelectedBy by = AutoPlaylist::SelectedBy::LeastRecentlyAdded)
{
    AutoPlaylist p;
    p.filterDuplicates = true;
    p.selectedBy = by;
    linuxbee::RuleContext c;
    c.now = QDateTime(QDate(2026, 11, 30), QTime(12, 0), QTimeZone::UTC);
    return linuxbee::evaluateAutoPlaylist(p, tracks, c, 1);
}

}

class AutoPlaylistDuplicatesTest : public QObject {
    Q_OBJECT
private slots:
    void onAnotherAlbum();
    void foldedAndTrimmed();
    void allTheArtists();
    void notTheSame();
    void aBlankTitle();
    void theFirstGivenIsKept();
    void offKeepsAll();
};

void AutoPlaylistDuplicatesTest::onAnotherAlbum()
{
    // W-AP: the same song on two albums is one.
    QCOMPARE(run({ make(1, { s(u"Adele") }, u"Hello", u"25", 1),
                 make(2, { s(u"Adele") }, u"Hello", u"Live", 2) }),
        QList<qint64>({ 1 }));
    // The album artist does not count either (each track has its own here).
    QCOMPARE(run({ make(1, { s(u"Muse") }, u"Uprising", u"A", 1),
                 make(2, { s(u"Muse") }, u"Uprising", u"A", 2),
                 make(3, { s(u"Muse") }, u"Uprising", u"B", 3) }),
        QList<qint64>({ 1 }));
}

void AutoPlaylistDuplicatesTest::foldedAndTrimmed()
{
    // A-DUP: case, accents and outer spaces ignored (S-FOLD), as before.
    QCOMPARE(run({ make(1, { s(u"Bj\u00f6rk") }, u"J\u00f3ga", u"Homogenic", 1),
                 make(2, { s(u"BJORK ") }, u" joga", u"Greatest Hits", 2) }),
        QList<qint64>({ 1 }));
}

void AutoPlaylistDuplicatesTest::allTheArtists()
{
    // A-DUP: every artist value, joined by "; ", in order.
    const QList<LibraryTrack> l
        = { make(1, { s(u"Jay-Z"), s(u"Kanye West") }, u"Otis", u"Watch the Throne", 1),
              make(2, { s(u"Jay-Z; Kanye West") }, u"Otis", u"Single", 2),
              make(3, { s(u"Jay-Z") }, u"Otis", u"Other", 3),
              make(4, { s(u"Kanye West"), s(u"Jay-Z") }, u"Otis", u"Other", 4) };
    QCOMPARE(run(l), QList<qint64>({ 1, 3, 4 }));
}

void AutoPlaylistDuplicatesTest::notTheSame()
{
    QCOMPARE(run({ make(1, { s(u"Adele") }, u"Hello", u"25", 1),
                 make(2, { s(u"Lionel Richie") }, u"Hello", u"25", 2),
                 make(3, { s(u"Adele") }, u"Hello (Live)", u"25", 3) }),
        QList<qint64>({ 1, 2, 3 }));
}

void AutoPlaylistDuplicatesTest::aBlankTitle()
{
    // A-DUP: a track with no title (none, empty or spaces) is never a duplicate.
    QCOMPARE(run({ make(1, { s(u"Boards of Canada") }, nullptr, u"A", 1),
                 make(2, { s(u"Boards of Canada") }, nullptr, u"B", 2),
                 make(3, { s(u"Boards of Canada") }, u"  ", u"C", 3),
                 make(4, { s(u"Boards of Canada") }, u"", u"D", 4) }),
        QList<qint64>({ 1, 2, 3, 4 }));
}

void AutoPlaylistDuplicatesTest::theFirstGivenIsKept()
{
    // A-DUP: the first in the order the tracks were given, before any selection (linuxBee's; W-AP
    // names the duplicate manager's hierarchy, which no public page describes).
    const QList<LibraryTrack> l = { make(5, { s(u"Adele") }, u"Hello", u"25", 1),
        make(2, { s(u"Adele") }, u"Hello", u"Live", 9), make(9, { s(u"Muse") }, u"Gone", u"X", 5) };
    QCOMPARE(run(l), QList<qint64>({ 5, 9 }));
    QCOMPARE(run(l, AutoPlaylist::SelectedBy::MostRecentlyAdded), QList<qint64>({ 9, 5 }));
}

void AutoPlaylistDuplicatesTest::offKeepsAll()
{
    AutoPlaylist p;
    p.selectedBy = AutoPlaylist::SelectedBy::LeastRecentlyAdded;
    linuxbee::RuleContext c;
    c.now = QDateTime(QDate(2026, 11, 30), QTime(12, 0), QTimeZone::UTC);
    QCOMPARE(linuxbee::evaluateAutoPlaylist(p,
                 { make(1, { s(u"Adele") }, u"Hello", u"25", 1),
                     make(2, { s(u"Adele") }, u"Hello", u"Live", 2) },
                 c, 1),
        QList<qint64>({ 1, 2 }));
}

QTEST_GUILESS_MAIN(AutoPlaylistDuplicatesTest)
#include "tst_autoplaylist_duplicates.moc"
