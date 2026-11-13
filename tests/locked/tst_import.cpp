// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Every expected value is a rule in docs/spec/import.md (I-x) applied by hand
// to tests/data/import/library.xml, a hand-written file in the publicly known
// iTunes library XML layout (see the README next to it).
#include <QtTest>

#include "import/itunesxml.h"

using linuxbee::ImportedLibrary;
using linuxbee::LibraryTrack;
using linuxbee::PathMapping;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

QDateTime utc(int y, int mo, int d, int h = 0, int mi = 0, int sec = 0)
{
    return QDateTime(QDate(y, mo, d), QTime(h, mi, sec), QTimeZone::UTC);
}

QByteArray fixtureBytes()
{
    QFile f(QStringLiteral(LINUXBEE_TEST_DATA "/import/library.xml"));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

LibraryTrack have(qint64 id, const char16_t *path, int plays = 0, int skips = 0, QDateTime last = { })
{
    LibraryTrack t;
    t.id = id;
    t.path = s(path);
    t.dateAdded = utc(2026, 11, 13, 12);
    t.playCount = plays;
    t.skipCount = skips;
    t.lastPlayed = last;
    return t;
}

const PathMapping kMapping = { { s(u"C:\\Docs\\someone\\Music\\"), s(u"/srv/Music") },
    { s(u"//NAS/share"), s(u"/mnt/nas/") } };

}

class ImportTest : public QObject {
    Q_OBJECT
private slots:
    void location_data();
    void location();
    void mapping_data();
    void mapping();
    void parse();
    void parseErrors_data();
    void parseErrors();
    void plan();
    void planPlaylists();
};

void ImportTest::location_data()
{
    QTest::addColumn<QString>("url");
    QTest::addColumn<QString>("expected");
    auto row = [](const char *name, const char16_t *url, const char16_t *expected) {
        QTest::newRow(name) << QString::fromUtf16(url) << QString::fromUtf16(expected);
    };
    // I-LOCATION
    row("windows-localhost", u"file://localhost/C:/Docs/someone/Music/a.mp3", u"C:/Docs/someone/Music/a.mp3");
    row("windows-no-host", u"file:///C:/Music/a.mp3", u"C:/Music/a.mp3");
    row("percent", u"file://localhost/C:/M/01%20Hello%20%26%20Bye%20%2B%201.mp3", u"C:/M/01 Hello & Bye + 1.mp3");
    row("utf8", u"file:///C:/M/Bj%C3%B6rk/J%C3%B3ga.flac", u"C:/M/Bj\u00f6rk/J\u00f3ga.flac");
    row("plus-is-a-plus", u"file:///C:/M/a+b.mp3", u"C:/M/a+b.mp3");
    row("mac", u"file://localhost/Volumes/Media/b.mp3", u"/Volumes/Media/b.mp3");
    row("unix-no-host", u"file:///srv/Music/c.ogg", u"/srv/Music/c.ogg");
    row("unc", u"file://nas/share/Music/a.ogg", u"//nas/share/Music/a.ogg");
    row("scheme-case-and-spaces", u"  FILE://LocalHost/C:/M/a.mp3 ", u"C:/M/a.mp3");
    row("folder", u"file://localhost/C:/Docs/someone/Music/", u"C:/Docs/someone/Music");
    row("cleaned", u"file:///C:/M//x/../a.mp3", u"C:/M/a.mp3");
    row("percent-25", u"file:///C:/M/100%25.mp3", u"C:/M/100%.mp3");
    row("http", u"http://example.invalid/stream", u"");
    row("plain-path", u"C:/M/a.mp3", u"");
    row("empty", u"", u"");
    row("no-path", u"file://localhost", u"");
}

void ImportTest::location()
{
    QFETCH(QString, url);
    QFETCH(QString, expected);
    QCOMPARE(linuxbee::pathFromItunesLocation(url), expected);
}

void ImportTest::mapping_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<QString>("expected");
    auto row = [](const char *name, const char16_t *path, const char16_t *expected) {
        QTest::newRow(name) << QString::fromUtf16(path) << QString::fromUtf16(expected);
    };
    // I-MAP with: C:\Docs\someone\Music\ -> /srv/Music   and   //NAS/share -> /mnt/nas/
    row("mapped", u"C:/Docs/someone/Music/Adele/25/01 Hello.mp3", u"/srv/Music/Adele/25/01 Hello.mp3");
    row("drive-case-ignored", u"c:/docs/SOMEONE/music/Adele/a.mp3", u"/srv/Music/Adele/a.mp3"); // the rest keeps its case
    row("backslashes", u"C:\\Docs\\someone\\Music\\x.mp3", u"/srv/Music/x.mp3");
    row("folder-not-text", u"C:/Docs/someone/Music2/x.mp3", u"C:/Docs/someone/Music2/x.mp3");
    row("the-folder-itself", u"C:/Docs/someone/Music", u"C:/Docs/someone/Music"); // only what is under it
    row("unc", u"//nas/share/Music/a.ogg", u"/mnt/nas/Music/a.ogg");
    row("unmapped", u"D:/Other/x.m4a", u"D:/Other/x.m4a");
    row("unmapped-cleaned", u"/Volumes//Media/./b.mp3", u"/Volumes/Media/b.mp3");
}

void ImportTest::mapping()
{
    QFETCH(QString, path);
    QFETCH(QString, expected);
    QCOMPARE(linuxbee::mapImportedPath(path, kMapping), expected);
    if (QTest::currentDataTag() == QByteArray("mapped")) {
        // The first pair that fits is used; an empty "from" fits nothing; no mapping is no change.
        const PathMapping two = { { QString(), s(u"/nowhere") }, { s(u"C:/Docs"), s(u"/first") }, { s(u"C:/Docs/someone"), s(u"/second") } };
        QCOMPARE(linuxbee::mapImportedPath(path, two), s(u"/first/someone/Music/Adele/25/01 Hello.mp3"));
        QCOMPARE(linuxbee::mapImportedPath(path, { }), path);
        // A Unix "from" is matched with its case.
        const PathMapping unixy = { { s(u"/volumes/media"), s(u"/a") }, { s(u"/Volumes/Media"), s(u"/b") } };
        QCOMPARE(linuxbee::mapImportedPath(s(u"/Volumes/Media/b.mp3"), unixy), s(u"/b/b.mp3"));
        // Mapping to the root.
        QCOMPARE(linuxbee::mapImportedPath(s(u"C:/Music/a.mp3"), { { s(u"C:/"), s(u"/") } }), s(u"/Music/a.mp3"));
    }
}

void ImportTest::parse()
{
    // I-PARSE
    const ImportedLibrary lib = linuxbee::parseItunesLibrary(fixtureBytes());
    QVERIFY2(lib.ok, qPrintable(lib.error));
    QVERIFY(lib.error.isEmpty());
    // 1004 has no location and 1005 is a stream: six tracks, in file order.
    QList<int> ids;
    for (const auto &t : lib.tracks)
        ids.append(t.trackId);
    QCOMPARE(ids, QList<int>({ 1001, 1002, 1003, 1006, 1007, 1008 }));

    const auto &hello = lib.tracks.at(0);
    QCOMPARE(hello.location, s(u"C:/Docs/someone/Music/Adele/25/01 Hello.mp3"));
    QCOMPARE(hello.name, s(u"Hello"));
    QCOMPARE(hello.artist, s(u"Adele"));
    QCOMPARE(hello.album, s(u"25"));
    QVERIFY(hello.rating && *hello.rating == 5.0);
    QCOMPARE(hello.playCount, 12);
    QCOMPARE(hello.skipCount, 2);
    QCOMPARE(hello.dateAdded, utc(2019, 3, 4, 7));
    QCOMPARE(hello.lastPlayed, utc(2025, 12, 31, 23, 59, 59)); // "Play Date UTC"; the "Play Date" number is ignored
    QVERIFY(hello.dateAdded.timeSpec() == Qt::UTC);

    const auto &joga = lib.tracks.at(1); // keys in another order, a <real> among them
    QCOMPARE(joga.location, s(u"C:/Docs/someone/Music/Bj\u00f6rk/J\u00f3ga.flac"));
    QCOMPARE(joga.name, s(u"J\u00f3ga"));
    QCOMPARE(joga.artist, s(u"Bj\u00f6rk"));
    QVERIFY(joga.rating && *joga.rating == 4.5); // 90
    QCOMPARE(joga.playCount, 0);
    QCOMPARE(joga.skipCount, 0);
    QVERIFY(!joga.lastPlayed.isValid());
    QCOMPARE(joga.dateAdded, utc(2020, 6, 7, 8, 9, 10));

    const auto &zep = lib.tracks.at(2);
    QCOMPARE(zep.name, s(u"Rock & Roll <live>")); // XML entities
    QCOMPARE(zep.location, s(u"D:/Other/x&y + z.m4a"));
    QVERIFY(!zep.rating); // "Rating Computed": the album's rating, not the track's
    QCOMPARE(zep.playCount, 3);
    QCOMPARE(zep.album, QString());

    QCOMPARE(lib.tracks.at(3).location, s(u"//nas/share/Music/a.ogg"));
    const auto &mac = lib.tracks.at(4);
    QCOMPARE(mac.location, s(u"/Volumes/Media/b.mp3"));
    QVERIFY(mac.rating && *mac.rating == 1.0); // 15 is nearer 20 than 10
    QCOMPARE(mac.playCount, 0); // a negative count is 0
    QCOMPARE(mac.skipCount, 4);
    QVERIFY(!mac.dateAdded.isValid());

    // Playlists: the master list, the built-in lists, smart playlists, folders and
    // unnamed lists are left out. Names are trimmed.
    QCOMPARE(lib.playlists.size(), 3);
    QCOMPARE(lib.playlists.at(0).name, s(u"Road Trip"));
    QCOMPARE(lib.playlists.at(0).trackIds, QList<int>({ 1002, 1001, 1004, 1001 })); // as written, known or not
    QCOMPARE(lib.playlists.at(1).name, s(u"Empty"));
    QVERIFY(lib.playlists.at(1).trackIds.isEmpty());
    QCOMPARE(lib.playlists.at(2).name, s(u"Dupes & NAS"));
    QCOMPARE(lib.playlists.at(2).trackIds, QList<int>({ 1008, 1006, 4242 }));

    // A library with no playlists and no tracks is still a library.
    const ImportedLibrary bare = linuxbee::parseItunesLibrary(
        "<?xml version=\"1.0\"?><plist version=\"1.0\"><dict><key>Tracks</key><dict/></dict></plist>");
    QVERIFY(bare.ok);
    QVERIFY(bare.tracks.isEmpty() && bare.playlists.isEmpty());
    // A byte order mark and Windows line ends do not matter.
    QByteArray windows = fixtureBytes();
    windows.replace("\n", "\r\n");
    QCOMPARE(linuxbee::parseItunesLibrary("\xEF\xBB\xBF" + windows).tracks.size(), 6);
}

void ImportTest::parseErrors_data()
{
    QTest::addColumn<QByteArray>("xml");
    const QByteArray good = fixtureBytes();
    QTest::newRow("empty") << QByteArray();
    QTest::newRow("not-xml") << QByteArray("this is not a library");
    QTest::newRow("other-xml") << QByteArray("<?xml version=\"1.0\"?><rss><channel/></rss>");
    QTest::newRow("plist-array") << QByteArray("<plist version=\"1.0\"><array/></plist>");
    QTest::newRow("no-tracks") << QByteArray("<plist version=\"1.0\"><dict><key>Playlists</key><array/></dict></plist>");
    QTest::newRow("tracks-not-dict") << QByteArray("<plist version=\"1.0\"><dict><key>Tracks</key><array/></dict></plist>");
    QTest::newRow("truncated") << good.left(good.size() / 2);
    QTest::newRow("truncated-late") << good.left(good.size() - 10);
    QTest::newRow("stray-bracket") << QByteArray(good).replace("Hello", "He<llo");
    QTest::newRow("control-character") << QByteArray(good).replace("Adele", "Ad\x01" "ele"); // not allowed in XML 1.0
}

void ImportTest::parseErrors()
{
    QFETCH(QByteArray, xml);
    const ImportedLibrary lib = linuxbee::parseItunesLibrary(xml);
    QVERIFY(!lib.ok);
    QVERIFY(!lib.error.isEmpty());
    QVERIFY(lib.tracks.isEmpty()); // I-PARSE: all or nothing
    QVERIFY(lib.playlists.isEmpty());
    QVERIFY(linuxbee::planImport(lib, { have(1, u"/a.mp3") }, { }).matches.isEmpty());
}

void ImportTest::plan()
{
    // I-PLAN
    const ImportedLibrary lib = linuxbee::parseItunesLibrary(fixtureBytes());
    QVERIFY(lib.ok);
    const QList<LibraryTrack> library = {
        have(11, u"/srv/Music/Adele/25/01 Hello.mp3", 20, 0, utc(2024, 1, 1)), // played more here, but longer ago
        have(12, u"/srv/Music/Bj\u00f6rk/J\u00f3ga.flac", 3, 1, utc(2026, 1, 1)), // the file knows nothing of plays
        have(13, u"/mnt/nas/music/A.OGG"), // differs from the mapped path by case only
        have(14, u"/Volumes/Media/b.mp3", 0, 4), // already says what the file says, bar the date
        have(15, u"/srv/Music/not-in-the-file.mp3", 5),
    };
    const linuxbee::ImportPlan p = linuxbee::planImport(lib, library, kMapping);
    QCOMPARE(p.unmatched, QStringList({ s(u"D:/Other/x&y + z.m4a") }));
    QCOMPARE(p.matches.size(), 4); // 1001, 1002, 1006, 1007; 1008 is the same file as 1001

    const auto &hello = p.matches.at(0);
    QCOMPARE(hello.libraryId, qint64(11));
    QCOMPARE(hello.libraryPath, s(u"/srv/Music/Adele/25/01 Hello.mp3"));
    QCOMPARE(hello.statistics.dateAdded, utc(2019, 3, 4, 7)); // the file's date added wins
    QCOMPARE(hello.statistics.playCount, 20); // the larger count
    QCOMPARE(hello.statistics.skipCount, 2);
    QCOMPARE(hello.statistics.lastPlayed, utc(2025, 12, 31, 23, 59, 59)); // the later time
    QVERIFY(hello.rating && *hello.rating == 5.0);
    QVERIFY(hello.changes);

    const auto &joga = p.matches.at(1);
    QCOMPARE(joga.libraryId, qint64(12));
    QCOMPARE(joga.statistics.playCount, 3);
    QCOMPARE(joga.statistics.skipCount, 1);
    QCOMPARE(joga.statistics.lastPlayed, utc(2026, 1, 1)); // the file has none: kept
    QCOMPARE(joga.statistics.dateAdded, utc(2020, 6, 7, 8, 9, 10));
    QVERIFY(joga.changes); // the date added

    const auto &nas = p.matches.at(2);
    QCOMPARE(nas.libraryId, qint64(13)); // one track fits when case is ignored
    QCOMPARE(nas.libraryPath, s(u"/mnt/nas/music/A.OGG"));
    QCOMPARE(nas.statistics.playCount, 1);
    QCOMPARE(nas.statistics.lastPlayed, utc(2022, 3, 3, 3, 3, 3));
    QVERIFY(!nas.rating);

    const auto &mac = p.matches.at(3);
    QCOMPARE(mac.libraryId, qint64(14));
    QCOMPARE(mac.statistics.dateAdded, utc(2026, 11, 13, 12)); // the file has none: the library's is kept
    QCOMPARE(mac.statistics.playCount, 0);
    QCOMPARE(mac.statistics.skipCount, 4);
    QVERIFY(!mac.statistics.lastPlayed.isValid());
    QVERIFY(!mac.changes); // nothing would change

    // Applying the plan and planning again changes nothing more.
    QList<LibraryTrack> after = library;
    for (const auto &m : p.matches) {
        for (LibraryTrack &t : after) {
            if (t.id != m.libraryId)
                continue;
            t.dateAdded = m.statistics.dateAdded;
            t.playCount = m.statistics.playCount;
            t.skipCount = m.statistics.skipCount;
            t.lastPlayed = m.statistics.lastPlayed;
        }
    }
    for (const auto &m : linuxbee::planImport(lib, after, kMapping).matches)
        QVERIFY(!m.changes);

    // Two library tracks that fit by case only: neither is chosen.
    QList<LibraryTrack> twins = { have(21, u"/mnt/nas/music/A.OGG"), have(22, u"/mnt/nas/MUSIC/a.ogg") };
    QVERIFY(linuxbee::planImport(lib, twins, kMapping).matches.isEmpty());
    twins.append(have(23, u"/mnt/nas/Music/a.ogg")); // an exact fit always wins
    QCOMPARE(linuxbee::planImport(lib, twins, kMapping).matches.at(0).libraryId, qint64(23));
    // Without the mapping nothing fits but the Mac path.
    const linuxbee::ImportPlan unmapped = linuxbee::planImport(lib, library, { });
    QCOMPARE(unmapped.matches.size(), 1);
    QCOMPARE(unmapped.unmatched.size(), 5);
    QVERIFY(linuxbee::planImport(lib, { }, kMapping).matches.isEmpty());
}

void ImportTest::planPlaylists()
{
    const ImportedLibrary lib = linuxbee::parseItunesLibrary(fixtureBytes());
    QVERIFY(lib.ok);
    const QList<LibraryTrack> library = { have(11, u"/srv/Music/Adele/25/01 Hello.mp3"),
        have(12, u"/srv/Music/Bj\u00f6rk/J\u00f3ga.flac"), have(13, u"/mnt/nas/Music/a.ogg") };
    const linuxbee::ImportPlan p = linuxbee::planImport(lib, library, kMapping);
    QCOMPARE(p.playlists.size(), 3);
    // Road Trip: 1002, 1001, 1004 (not a track), 1001 again
    QCOMPARE(p.playlists.at(0).name, s(u"Road Trip"));
    QCOMPARE(p.playlists.at(0).paths,
        QStringList({ s(u"/srv/Music/Bj\u00f6rk/J\u00f3ga.flac"), s(u"/srv/Music/Adele/25/01 Hello.mp3"),
            s(u"/srv/Music/Adele/25/01 Hello.mp3") }));
    QCOMPARE(p.playlists.at(0).missing, 1);
    QCOMPARE(p.playlists.at(1).name, s(u"Empty"));
    QVERIFY(p.playlists.at(1).paths.isEmpty());
    QCOMPARE(p.playlists.at(1).missing, 0);
    // Dupes & NAS: 1008 is the second entry for the Hello file and still finds it; 4242 is unknown.
    QCOMPARE(p.playlists.at(2).paths,
        QStringList({ s(u"/srv/Music/Adele/25/01 Hello.mp3"), s(u"/mnt/nas/Music/a.ogg") }));
    QCOMPARE(p.playlists.at(2).missing, 1);
    // With nothing in the library every playlist is there, and empty.
    const linuxbee::ImportPlan none = linuxbee::planImport(lib, { }, kMapping);
    QCOMPARE(none.playlists.size(), 3);
    QCOMPARE(none.playlists.at(0).missing, 4);
    QVERIFY(none.playlists.at(0).paths.isEmpty());
}

QTEST_GUILESS_MAIN(ImportTest)
#include "tst_import.moc"
