// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Every expected value is a rule in docs/spec/playlists.md (Y-x) worked by
// hand. The M3U and PLS layouts themselves are the long-standing de facto
// formats (FMT in the spec).
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "playlists/playlistformat.h"
#include "playlists/playliststore.h"

using linuxbee::PlaylistEntry;
using linuxbee::PlaylistFormat;
using linuxbee::PlaylistStore;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

QList<PlaylistEntry> parse(const char *text, PlaylistFormat format = PlaylistFormat::M3u8)
{
    return linuxbee::parsePlaylist(QByteArray(text), format, QStringLiteral("/lists"));
}

QStringList paths(const QList<PlaylistEntry> &entries)
{
    QStringList out;
    for (const PlaylistEntry &e : entries)
        out.append(e.path);
    return out;
}

}

class PlaylistsTest : public QObject {
    Q_OBJECT
private slots:
    void formatForFile();
    void readM3u();
    void readPaths_data();
    void readPaths();
    void readEncoding();
    void readPls();
    void write();
    void roundTrip();
    void names_data();
    void names();
    void storeList();
    void storeTracks();
    void storeRefusals();
};

void PlaylistsTest::formatForFile()
{
    // Y-FORMAT
    QVERIFY(linuxbee::playlistFormatForFile(s(u"a.m3u")) == PlaylistFormat::M3u);
    QVERIFY(linuxbee::playlistFormatForFile(s(u"/x/My List.M3U8")) == PlaylistFormat::M3u8);
    QVERIFY(linuxbee::playlistFormatForFile(s(u"a.b.Pls")) == PlaylistFormat::Pls);
    QVERIFY(!linuxbee::playlistFormatForFile(s(u"a.mp3")));
    QVERIFY(!linuxbee::playlistFormatForFile(s(u"m3u")));
    QVERIFY(!linuxbee::playlistFormatForFile(s(u"a.m3u.bak")));
    QVERIFY(!linuxbee::playlistFormatForFile(QString()));
}

void PlaylistsTest::readM3u()
{
    // Y-READ, M3U
    const QList<PlaylistEntry> e = parse("#EXTM3U\r\n"
                                         "#EXTINF:215,Adele - Hello\r\n"
                                         "/music/a.mp3\r\n"
                                         "\r\n"
                                         "# a comment\r\n"
                                         "/music/b.flac\n"
                                         "#EXTINF:-1,Live, at the BBC\n"
                                         "#EXTVLCOPT:something\n"
                                         "/music/c.ogg\n"
                                         "#EXTINF:12.6\n"
                                         "  /music/d.mp3  \n"
                                         "#extinf: 7 , spaced \n"
                                         "/music/e.mp3");
    QCOMPARE(e.size(), 5);
    QCOMPARE(e.at(0), (PlaylistEntry { s(u"/music/a.mp3"), s(u"Adele - Hello"), 215 }));
    QCOMPARE(e.at(1), (PlaylistEntry { s(u"/music/b.flac"), QString(), -1 })); // no #EXTINF of its own
    QCOMPARE(e.at(2), (PlaylistEntry { s(u"/music/c.ogg"), s(u"Live, at the BBC"), -1 })); // first comma splits
    QCOMPARE(e.at(3), (PlaylistEntry { s(u"/music/d.mp3"), QString(), 13 })); // rounded; no title
    QCOMPARE(e.at(4), (PlaylistEntry { s(u"/music/e.mp3"), s(u"spaced"), 7 }));
    // Without the header, and with old Mac line ends.
    QCOMPARE(paths(parse("/a.mp3\r/b.mp3\r")), QStringList({ s(u"/a.mp3"), s(u"/b.mp3") }));
    QVERIFY(parse("").isEmpty());
    QVERIFY(parse("#EXTM3U\n#EXTINF:1,x\n").isEmpty()); // an #EXTINF with no path after it
}

void PlaylistsTest::readPaths_data()
{
    QTest::addColumn<QString>("line");
    QTest::addColumn<QString>("expected");
    auto row = [](const char *name, const char16_t *line, const char16_t *expected) {
        QTest::newRow(name) << QString::fromUtf16(line) << QString::fromUtf16(expected);
    };
    // Y-PATH, with the playlist in /lists
    row("absolute", u"/music/a.mp3", u"/music/a.mp3");
    row("absolute-cleaned", u"/music//x/../a.mp3", u"/music/a.mp3");
    row("relative", u"a.mp3", u"/lists/a.mp3");
    row("relative-folder", u"sub/a.mp3", u"/lists/sub/a.mp3");
    row("relative-up", u"../music/a.mp3", u"/music/a.mp3");
    row("relative-dot", u"./a.mp3", u"/lists/a.mp3");
    row("backslashes", u"sub\\deep\\a.mp3", u"/lists/sub/deep/a.mp3");
    row("backslash-up", u"..\\music\\a.mp3", u"/music/a.mp3");
    row("mixed-slashes-kept", u"sub/odd\\name.mp3", u"/lists/sub/odd\\name.mp3"); // a real slash is there: nothing is guessed
    row("drive", u"C:\\Music\\a.mp3", u"C:/Music/a.mp3"); // kept for relinking; not made relative
    row("drive-slashes", u"D:/Music/a.mp3", u"D:/Music/a.mp3");
    row("file-url", u"file:///music/My%20Song.mp3", u"/music/My Song.mp3");
    row("http", u"http://example.invalid/stream?x=1", u"http://example.invalid/stream?x=1");
    row("https", u"https://example.invalid/a%20b.mp3", u"https://example.invalid/a%20b.mp3");
    row("spaces-and-unicode", u"Bj\u00f6rk - J\u00f3ga (live).mp3", u"/lists/Bj\u00f6rk - J\u00f3ga (live).mp3");
    row("colon-in-name", u"a: b.mp3", u"/lists/a: b.mp3"); // not a URL, not a drive
}

void PlaylistsTest::readPaths()
{
    QFETCH(QString, line);
    QFETCH(QString, expected);
    const auto m3u = linuxbee::parsePlaylist(line.toUtf8(), PlaylistFormat::M3u8, s(u"/lists"));
    QCOMPARE(paths(m3u), QStringList({ expected }));
    const auto pls = linuxbee::parsePlaylist("[playlist]\nFile1=" + line.toUtf8() + "\n", PlaylistFormat::Pls, s(u"/lists"));
    QCOMPARE(paths(pls), QStringList({ expected }));
}

void PlaylistsTest::readEncoding()
{
    // Y-DECODE
    const QString name = s(u"/m/Bj\u00f6rk.mp3");
    const QByteArray utf8 = name.toUtf8();
    const QByteArray latin1 = name.toLatin1();
    QCOMPARE(paths(linuxbee::parsePlaylist(utf8, PlaylistFormat::M3u8, s(u"/l"))), QStringList({ name }));
    QCOMPARE(paths(linuxbee::parsePlaylist("\xEF\xBB\xBF" + utf8, PlaylistFormat::M3u8, s(u"/l"))), QStringList({ name }));
    QCOMPARE(paths(linuxbee::parsePlaylist("\xEF\xBB\xBF" + utf8, PlaylistFormat::M3u, s(u"/l"))), QStringList({ name }));
    // .m3u and .pls: UTF-8 when the bytes are valid UTF-8, otherwise Latin-1.
    QCOMPARE(paths(linuxbee::parsePlaylist(utf8, PlaylistFormat::M3u, s(u"/l"))), QStringList({ name }));
    QCOMPARE(paths(linuxbee::parsePlaylist(latin1, PlaylistFormat::M3u, s(u"/l"))), QStringList({ name }));
    QCOMPARE(paths(linuxbee::parsePlaylist("[playlist]\nFile1=" + latin1, PlaylistFormat::Pls, s(u"/l"))),
        QStringList({ name }));
    // .m3u8 is UTF-8 by definition: a bad byte becomes U+FFFD, the rest is kept.
    QCOMPARE(paths(linuxbee::parsePlaylist(latin1, PlaylistFormat::M3u8, s(u"/l"))),
        QStringList({ s(u"/m/Bj\ufffdrk.mp3") }));
}

void PlaylistsTest::readPls()
{
    // Y-READ, PLS: entries by number, whatever the order of the lines; keys ignore case.
    const QList<PlaylistEntry> e = parse("[playlist]\r\n"
                                         "NumberOfEntries=3\r\n"
                                         "File2=/music/b.mp3\r\n"
                                         "Title2=Second = two\r\n"
                                         "Length2=200\r\n"
                                         "file1 = rel/a.mp3 \r\n"
                                         "TITLE1=First\r\n"
                                         "Length1=-1\r\n"
                                         "Title5=No file for this one\r\n"
                                         "File7=http://example.invalid/s\r\n"
                                         "Length7=abc\r\n"
                                         "File0=/zero.mp3\r\n"
                                         "Filex=/x.mp3\r\n"
                                         "Version=2\r\n",
        PlaylistFormat::Pls);
    QCOMPARE(e.size(), 3);
    QCOMPARE(e.at(0), (PlaylistEntry { s(u"/lists/rel/a.mp3"), s(u"First"), -1 }));
    QCOMPARE(e.at(1), (PlaylistEntry { s(u"/music/b.mp3"), s(u"Second = two"), 200 }));
    QCOMPARE(e.at(2), (PlaylistEntry { s(u"http://example.invalid/s"), QString(), -1 }));
    QVERIFY(parse("", PlaylistFormat::Pls).isEmpty());
    QVERIFY(parse("[playlist]\nNumberOfEntries=0\n", PlaylistFormat::Pls).isEmpty());
}

void PlaylistsTest::write()
{
    const QList<PlaylistEntry> entries = {
        { s(u"/music/Adele/a.mp3"), s(u"Adele - Hello"), 215 },
        { s(u"/music/b.flac"), QString(), -1 },
        { s(u"/other/c.ogg"), s(u"Bj\u00f6rk"), -1 },
        { s(u"http://example.invalid/s"), QString(), 30 },
        { s(u"C:/Music/d.mp3"), QString(), -1 },
    };
    // Y-WRITE, M3U extended (the default)
    QCOMPARE(linuxbee::writePlaylist(entries, PlaylistFormat::M3u8),
        s(u"#EXTM3U\n"
          u"#EXTINF:215,Adele - Hello\n/music/Adele/a.mp3\n"
          u"/music/b.flac\n"
          u"#EXTINF:-1,Bj\u00f6rk\n/other/c.ogg\n"
          u"#EXTINF:30,\nhttp://example.invalid/s\n"
          u"C:/Music/d.mp3\n")
            .toUtf8());
    // .m3u is written exactly as .m3u8 is.
    QCOMPARE(linuxbee::writePlaylist(entries, PlaylistFormat::M3u), linuxbee::writePlaylist(entries, PlaylistFormat::M3u8));
    // plain, with relative paths: URLs and drive paths stay as they are
    linuxbee::PlaylistWriteOptions o;
    o.extended = false;
    o.relativePaths = true;
    o.baseFolder = s(u"/music/lists");
    QCOMPARE(linuxbee::writePlaylist(entries, PlaylistFormat::M3u8, o),
        QByteArray("../Adele/a.mp3\n../b.flac\n../../other/c.ogg\nhttp://example.invalid/s\nC:/Music/d.mp3\n"));
    o.baseFolder = s(u"/music");
    QCOMPARE(linuxbee::writePlaylist({ entries.at(0) }, PlaylistFormat::M3u8, o), QByteArray("Adele/a.mp3\n"));
    o.baseFolder.clear(); // relative to nothing: absolute
    QCOMPARE(linuxbee::writePlaylist({ entries.at(0) }, PlaylistFormat::M3u8, o), QByteArray("/music/Adele/a.mp3\n"));
    // nothing to write
    QCOMPARE(linuxbee::writePlaylist({ }, PlaylistFormat::M3u8), QByteArray("#EXTM3U\n"));
    QCOMPARE(linuxbee::writePlaylist({ }, PlaylistFormat::M3u8, o), QByteArray());
    // Y-WRITE, PLS
    QCOMPARE(linuxbee::writePlaylist({ entries.at(0), entries.at(1) }, PlaylistFormat::Pls),
        QByteArray("[playlist]\n"
                   "File1=/music/Adele/a.mp3\nTitle1=Adele - Hello\nLength1=215\n"
                   "File2=/music/b.flac\nLength2=-1\n"
                   "NumberOfEntries=2\nVersion=2\n"));
    QCOMPARE(linuxbee::writePlaylist({ }, PlaylistFormat::Pls), QByteArray("[playlist]\nNumberOfEntries=0\nVersion=2\n"));
}

void PlaylistsTest::roundTrip()
{
    const QList<PlaylistEntry> entries = {
        { s(u"/music/Adele/a.mp3"), s(u"Adele - Hello, again"), 215 },
        { s(u"/music/\u65e5\u672c/b.flac"), QString(), -1 },
        { s(u"https://example.invalid/s"), s(u"Radio"), -1 },
    };
    for (const PlaylistFormat format : { PlaylistFormat::M3u, PlaylistFormat::M3u8, PlaylistFormat::Pls }) {
        for (const bool relative : { false, true }) {
            linuxbee::PlaylistWriteOptions o;
            o.relativePaths = relative;
            o.baseFolder = s(u"/music/lists");
            const QByteArray bytes = linuxbee::writePlaylist(entries, format, o);
            QCOMPARE(linuxbee::parsePlaylist(bytes, format, s(u"/music/lists")), entries);
        }
    }
}

void PlaylistsTest::names_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<bool>("ok");
    // Y-NAME
    QTest::newRow("plain") << s(u"Favourites") << true;
    QTest::newRow("spaces-inside") << s(u"Road trip 2026") << true;
    QTest::newRow("unicode") << s(u"\u65e5\u672c\u8a9e \u00e9t\u00e9") << true;
    QTest::newRow("punctuation") << s(u"80's: best & worst (v2)!") << true;
    QTest::newRow("dot-inside") << s(u"vol. 1") << true;
    QTest::newRow("empty") << QString() << false;
    QTest::newRow("blank") << s(u"   ") << false;
    QTest::newRow("leading-space") << s(u" x") << false;
    QTest::newRow("trailing-space") << s(u"x ") << false;
    QTest::newRow("slash") << s(u"a/b") << false;
    QTest::newRow("backslash") << s(u"a\\b") << false;
    QTest::newRow("leading-dot") << s(u".hidden") << false;
    QTest::newRow("dot") << s(u".") << false;
    QTest::newRow("dotdot") << s(u"..") << false;
    QTest::newRow("newline") << s(u"a\nb") << false;
    QTest::newRow("tab") << s(u"a\tb") << false;
    QTest::newRow("200-bytes") << QString(200, QLatin1Char('x')) << true;
    QTest::newRow("201-bytes") << QString(201, QLatin1Char('x')) << false;
    QTest::newRow("101-two-byte") << QString(101, QChar(0x00e9)) << false; // 202 bytes of UTF-8
}

void PlaylistsTest::names()
{
    QFETCH(QString, name);
    QFETCH(bool, ok);
    QCOMPARE(PlaylistStore::validateName(name).isEmpty(), ok);
}

void PlaylistsTest::storeList()
{
    // Y-STORE
    QTemporaryDir dir;
    const QString root = dir.path() + s(u"/Playlists");
    PlaylistStore store(root + s(u"/"));
    QCOMPARE(store.folder(), root);
    QVERIFY(store.playlists().isEmpty()); // the folder need not exist yet
    QVERIFY(store.folders().isEmpty());
    QSignalSpy list(&store, &PlaylistStore::listChanged);

    QCOMPARE(store.create(s(u"zebra")), s(u"zebra"));
    QCOMPARE(store.create(s(u"Apple")), s(u"Apple"));
    QCOMPARE(store.create(s(u"Favourites"), s(u"Rock/90s")), s(u"Rock/90s/Favourites"));
    QCOMPARE(list.size(), 3);
    QVERIFY(QFileInfo(root + s(u"/Rock/90s/Favourites.m3u8")).isFile()); // an ordinary file others can read
    QVERIFY(store.exists(s(u"Apple")));
    QVERIFY(!store.exists(s(u"apple"))); // ids are exact
    QVERIFY(!store.exists(s(u"Rock")));
    QVERIFY(!store.exists(QString()));

    // sorted by id, case ignored
    const QList<linuxbee::PlaylistInfo> all = store.playlists();
    QCOMPARE(all.size(), 3);
    QCOMPARE(all.at(0), (linuxbee::PlaylistInfo { s(u"Apple"), s(u"Apple"), QString() }));
    QCOMPARE(all.at(1), (linuxbee::PlaylistInfo { s(u"Rock/90s/Favourites"), s(u"Favourites"), s(u"Rock/90s") }));
    QCOMPARE(all.at(2), (linuxbee::PlaylistInfo { s(u"zebra"), s(u"zebra"), QString() }));
    QVERIFY(store.createFolder(s(u"Empty")));
    QCOMPARE(list.size(), 4);
    QVERIFY(store.createFolder(s(u"Empty"))); // there already: fine, and nothing changed
    QCOMPARE(list.size(), 4);
    QCOMPARE(store.folders(), QStringList({ s(u"Empty"), s(u"Rock"), s(u"Rock/90s") }));

    // Files put there by hand are seen; other files and hidden things are not.
    QFile hand(root + s(u"/Rock/by hand.m3u8"));
    QVERIFY(hand.open(QIODevice::WriteOnly));
    hand.write("/music/x.mp3\n");
    hand.close();
    QFile other(root + s(u"/notes.txt"));
    QVERIFY(other.open(QIODevice::WriteOnly));
    other.close();
    QDir().mkpath(root + s(u"/.trash"));
    QFile hidden(root + s(u"/.trash/old.m3u8"));
    QVERIFY(hidden.open(QIODevice::WriteOnly));
    hidden.close();
    QCOMPARE(store.playlists().size(), 4);
    QCOMPARE(store.playlists().at(2).id, s(u"Rock/by hand"));
    QCOMPARE(store.tracks(s(u"Rock/by hand")), QStringList({ s(u"/music/x.mp3") }));
    QVERIFY(!store.folders().contains(s(u".trash")));

    // rename keeps the folder and the tracks
    QCOMPARE(store.rename(s(u"Rock/by hand"), s(u"By Hand 2")), s(u"Rock/By Hand 2"));
    QCOMPARE(store.tracks(s(u"Rock/By Hand 2")), QStringList({ s(u"/music/x.mp3") }));
    QVERIFY(!store.exists(s(u"Rock/by hand")));
    QCOMPARE(list.size(), 5);
    QCOMPARE(store.rename(s(u"zebra"), s(u"zebra")), s(u"zebra")); // the same name: fine, nothing changed
    QCOMPARE(list.size(), 5);
    QCOMPARE(store.rename(s(u"zebra"), s(u"Zebra")), s(u"Zebra")); // a change of case only is a rename
    QCOMPARE(list.size(), 6);
    // move
    QCOMPARE(store.moveToFolder(s(u"Zebra"), s(u"Animals")), s(u"Animals/Zebra"));
    QCOMPARE(store.moveToFolder(s(u"Animals/Zebra"), QString()), s(u"Zebra"));
    QCOMPARE(store.moveToFolder(s(u"Zebra"), QString()), s(u"Zebra"));
    QCOMPARE(list.size(), 8);
    QVERIFY(store.folders().contains(s(u"Animals"))); // an emptied folder stays
    // remove
    QVERIFY(store.remove(s(u"Zebra")));
    QVERIFY(!store.exists(s(u"Zebra")));
    QVERIFY(!store.remove(s(u"Zebra")));
    QCOMPARE(list.size(), 9);
    // Another store on the same folder sees the same thing: nothing is kept in memory.
    PlaylistStore second(root);
    QCOMPARE(second.playlists(), store.playlists());
}

void PlaylistsTest::storeTracks()
{
    QTemporaryDir dir;
    PlaylistStore store(dir.path());
    const QString id = store.create(s(u"List"));
    QSignalSpy changed(&store, &PlaylistStore::tracksChanged);
    QVERIFY(store.tracks(id).isEmpty());
    const QString a = s(u"/music/a.mp3"), b = s(u"/music/b \u00e9.flac"), c = s(u"/music/c.ogg"), d = s(u"/music/d.mp3");
    QVERIFY(store.setTracks(id, { a, b, c }));
    QCOMPARE(store.tracks(id), QStringList({ a, b, c }));
    QCOMPARE(changed.size(), 1);
    QCOMPARE(changed.at(0).at(0).toString(), id);
    // The file is a plain list of absolute paths, in UTF-8.
    QFile f(dir.path() + s(u"/List.m3u8"));
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), (a + u'\n' + b + u'\n' + c + u'\n').toUtf8());
    f.close();

    // append: duplicates are skipped unless allowed
    QCOMPARE(store.append(id, { b, d, d }), 1);
    QCOMPARE(store.tracks(id), QStringList({ a, b, c, d }));
    QCOMPARE(store.append(id, { a, b }), 0);
    QCOMPARE(changed.size(), 2); // nothing added: no signal
    QCOMPARE(store.append(id, { a, a }, true), 2);
    QCOMPARE(store.tracks(id), QStringList({ a, b, c, d, a, a }));
    QCOMPARE(store.append(id, { }), 0);
    QCOMPARE(changed.size(), 3);

    // removeAt: by position; positions that do not exist are ignored
    QCOMPARE(store.removeAt(id, { 5, 0, 0, 99, -1 }), 2);
    QCOMPARE(store.tracks(id), QStringList({ b, c, d, a }));
    QCOMPARE(store.removeAt(id, { 99 }), 0);
    QCOMPARE(store.removeAt(id, { }), 0);
    QCOMPARE(changed.size(), 4);

    // moveTrack: the entry ends up at the position given
    QVERIFY(store.moveTrack(id, 0, 2));
    QCOMPARE(store.tracks(id), QStringList({ c, d, b, a }));
    QVERIFY(store.moveTrack(id, 3, 0));
    QCOMPARE(store.tracks(id), QStringList({ a, c, d, b }));
    QCOMPARE(changed.size(), 6);
    QVERIFY(store.moveTrack(id, 1, 1)); // nowhere to go: fine, nothing changed
    QCOMPARE(changed.size(), 6);
    QVERIFY(!store.moveTrack(id, 0, 4));
    QVERIFY(!store.moveTrack(id, -1, 0));
    QVERIFY(!store.moveTrack(id, 4, 0));
    QCOMPARE(store.tracks(id), QStringList({ a, c, d, b }));

    QVERIFY(store.setTracks(id, { }));
    QVERIFY(store.tracks(id).isEmpty());
    QVERIFY(store.exists(id)); // an empty playlist is still a playlist
}

void PlaylistsTest::storeRefusals()
{
    QTemporaryDir dir;
    PlaylistStore store(dir.path());
    QSignalSpy list(&store, &PlaylistStore::listChanged);
    QSignalSpy changed(&store, &PlaylistStore::tracksChanged);
    QString error;
    QCOMPARE(store.create(s(u"a/b"), QString(), &error), QString());
    QVERIFY(!error.isEmpty());
    QCOMPARE(store.create(QString()), QString()); // no error wanted
    QCOMPARE(store.create(s(u"ok"), s(u"../outside")), QString()); // Y-NAME applies to each folder part
    QCOMPARE(store.create(s(u"ok"), s(u"a//b")), QString());
    QCOMPARE(store.create(s(u"ok"), s(u"/abs")), QString());
    QVERIFY(!QFileInfo::exists(dir.path() + s(u"/../outside")));
    QVERIFY(!store.createFolder(QString(), &error));
    QVERIFY(!store.createFolder(s(u".x")));
    QCOMPARE(list.size(), 0);

    QCOMPARE(store.create(s(u"One")), s(u"One"));
    QCOMPARE(store.create(s(u"Two")), s(u"Two"));
    list.clear();
    error.clear();
    QCOMPARE(store.create(s(u"one"), QString(), &error), QString()); // names differ by more than case
    QVERIFY(!error.isEmpty());
    QCOMPARE(store.rename(s(u"Two"), s(u"ONE")), QString());
    QCOMPARE(store.rename(s(u"Two"), s(u"x/y")), QString());
    QCOMPARE(store.rename(s(u"Nope"), s(u"Three")), QString());
    QCOMPARE(store.moveToFolder(s(u"Nope"), s(u"F")), QString());
    QCOMPARE(store.moveToFolder(s(u"Two"), s(u"..")), QString());
    QCOMPARE(store.create(s(u"Two"), s(u"F")), s(u"F/Two"));
    list.clear();
    QCOMPARE(store.moveToFolder(s(u"Two"), s(u"F")), QString()); // F/Two is taken
    QVERIFY(store.exists(s(u"Two")));
    QCOMPARE(list.size(), 0);
    // An id never reaches outside the store.
    QFile outside(dir.path() + s(u"/../escape-test.m3u8"));
    QVERIFY(!store.exists(s(u"../escape-test")));
    QVERIFY(!store.setTracks(s(u"../escape-test"), { s(u"/x.mp3") }));
    QVERIFY(!outside.exists());
    // Unknown ids
    QVERIFY(store.tracks(s(u"Nope")).isEmpty());
    QVERIFY(!store.setTracks(s(u"Nope"), { s(u"/x.mp3") }));
    QCOMPARE(store.append(s(u"Nope"), { s(u"/x.mp3") }), 0);
    QCOMPARE(store.removeAt(s(u"Nope"), { 0 }), 0);
    QVERIFY(!store.moveTrack(s(u"Nope"), 0, 0));
    QVERIFY(!store.exists(s(u"Nope")));
    QCOMPARE(changed.size(), 0);
}

QTEST_GUILESS_MAIN(PlaylistsTest)
#include "tst_playlists.moc"
