// LOCKED ACCEPTANCE TEST (the playlist store: a list that was not read whole and true is not
// written). Written by the reviewer, not the builder. The builder makes these pass by changing
// src/, never by editing this file. A test believed to be wrong is reported, with the reason, and
// left alone.
//
// Expected values are Y-EDIT in docs/spec/playlists.md. The faults this holds shut (the write-path
// audit, part 4): F1, a playlist that could be written but not read was taken as empty, and one
// added track replaced the whole list; F2, any edit of a file that is not UTF-8 wrote every entry
// back mangled.
//
// Everything is in a temporary folder. The files are written here byte by byte.
#include <QtTest>

#include <unistd.h>

#include "playlists/playliststore.h"

using linuxbee::PlaylistStore;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

QByteArray bytesOf(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArrayLiteral("<cannot open>");
    return file.readAll();
}

QByteArray utf16(const QByteArray &ascii, bool big, bool mark)
{
    QByteArray out;
    if (mark)
        out += big ? QByteArrayLiteral("\xFE\xFF") : QByteArrayLiteral("\xFF\xFE");
    for (const char c : ascii) {
        if (big)
            out += '\0';
        out += c;
        if (!big)
            out += '\0';
    }
    return out;
}

const QString kNotRead = QStringLiteral("the file could not be read");
const QString kNotUtf8 = QStringLiteral("the file is not UTF-8");

}

class PlaylistsEditTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void notWhatCanBeEdited_data();
    void notWhatCanBeEdited();
    void whatCanBeEdited_data();
    void whatCanBeEdited();
    void noSuchPlaylist();

private:
    QString put(const char *name, const QByteArray &bytes);
    QTemporaryDir m_dir;
    QString m_root;
    int m_counter = 0;
};

void PlaylistsEditTest::init()
{
    QVERIFY(m_dir.isValid());
    m_root = m_dir.filePath(QStringLiteral("t%1").arg(++m_counter));
    QVERIFY(QDir().mkpath(m_root));
}

QString PlaylistsEditTest::put(const char *name, const QByteArray &bytes)
{
    const QString path = m_root + QLatin1Char('/') + QLatin1String(name) + QStringLiteral(".m3u8");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        QTest::qFail("could not write a playlist", __FILE__, __LINE__);
    return path;
}

void PlaylistsEditTest::notWhatCanBeEdited_data()
{
    QTest::addColumn<QByteArray>("bytes");
    QTest::addColumn<bool>("unreadable");
    QTest::addColumn<QString>("problem");
    const QByteArray three = "/m/a.mp3\n/m/b.mp3\n/m/c.mp3\n";
    QTest::newRow("cannot be read") << three << true << kNotRead;
    QTest::newRow("Latin-1") << QByteArray("/m/a.mp3\n/m/Bj\xF6rk/J\xF3ga.mp3\n/m/c.mp3\n") << false
                             << kNotUtf8;
    QTest::newRow("UTF-16, little end first, marked")
        << utf16(three, false, true) << false << kNotUtf8;
    QTest::newRow("UTF-16, big end first, marked") << utf16(three, true, true) << false << kNotUtf8;
    QTest::newRow("UTF-16, not marked") << utf16(three, false, false) << false << kNotUtf8;
    QTest::newRow("a byte that is no text")
        << QByteArray("/m/a.mp3\n/m/b\0.mp3\n/m/c.mp3\n", 28) << false << kNotUtf8;
    QTest::newRow("cut in the middle of a letter")
        << QByteArray("/m/a.mp3\n/m/Bj\xC3") << false << kNotUtf8;
}

void PlaylistsEditTest::notWhatCanBeEdited()
{
    // Y-EDIT: none of the four calls that write a playlist's entries writes this one. Each says it
    // did nothing, the bytes are the bytes, and nobody is told that the tracks changed.
    QFETCH(QByteArray, bytes);
    QFETCH(bool, unreadable);
    QFETCH(QString, problem);
    const QString path = put("List", bytes);
    if (unreadable) {
        QVERIFY2(
            ::geteuid() != 0, "this test cannot be run as root: nothing is unreadable to root");
        QVERIFY(QFile::setPermissions(path, QFile::WriteOwner));
        QVERIFY(!QFile(path).open(QIODevice::ReadOnly));
    }
    PlaylistStore store(m_root);
    QSignalSpy changed(&store, &PlaylistStore::tracksChanged);
    QVERIFY(store.exists(s(u"List")));
    QCOMPARE(store.editProblem(s(u"List")), problem);
    QCOMPARE(store.append(s(u"List"), { s(u"/m/d.mp3") }, true), 0);
    QCOMPARE(store.append(s(u"List"), { s(u"/m/d.mp3") }, false), 0);
    QCOMPARE(store.removeAt(s(u"List"), { 0 }), 0);
    QVERIFY(!store.moveTrack(s(u"List"), 0, 1));
    QVERIFY(!store.setTracks(s(u"List"), { s(u"/m/d.mp3") }));
    QCOMPARE(int(changed.size()), 0);
    if (unreadable)
        QVERIFY(QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner));
    QCOMPARE(bytesOf(path), bytes);
    QCOMPARE(QDir(m_root).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot),
        QStringList({ s(u"List.m3u8") }));
}

void PlaylistsEditTest::whatCanBeEdited_data()
{
    QTest::addColumn<QByteArray>("bytes");
    QTest::addColumn<int>("entries");
    QTest::newRow("plain") << QByteArray("/m/a.mp3\n/m/b.mp3\n") << 2;
    QTest::newRow("empty") << QByteArray() << 0;
    QTest::newRow("UTF-8 with letters beyond ASCII")
        << QByteArray("/m/Bj\xC3\xB6rk/J\xC3\xB3ga.mp3\n/m/\xE6\x97\xA5\xE6\x9C\xAC.mp3\n") << 2;
    QTest::newRow("a UTF-8 mark and CRLF")
        << QByteArray("\xEF\xBB\xBF/m/a.mp3\r\n/m/b.mp3\r\n") << 2;
    QTest::newRow("no line end at the end") << QByteArray("/m/a.mp3\n/m/b.mp3") << 2;
}

void PlaylistsEditTest::whatCanBeEdited()
{
    // The other side: what is UTF-8 is edited as before.
    QFETCH(QByteArray, bytes);
    QFETCH(int, entries);
    put("List", bytes);
    PlaylistStore store(m_root);
    QCOMPARE(store.editProblem(s(u"List")), QString());
    const QStringList before = store.tracks(s(u"List"));
    QCOMPARE(int(before.size()), entries);
    QCOMPARE(store.append(s(u"List"), { s(u"/m/d.mp3") }, true), 1);
    QCOMPARE(store.tracks(s(u"List")), before + QStringList({ s(u"/m/d.mp3") }));
    QCOMPARE(store.removeAt(s(u"List"), { entries }), 1);
    QCOMPARE(store.tracks(s(u"List")), before);
    QVERIFY(store.setTracks(s(u"List"), { s(u"/m/x.mp3") }));
    QCOMPARE(store.tracks(s(u"List")), QStringList({ s(u"/m/x.mp3") }));
}

void PlaylistsEditTest::noSuchPlaylist()
{
    PlaylistStore store(m_root);
    QCOMPARE(store.editProblem(s(u"Nothing")), s(u"there is no such playlist"));
    QCOMPARE(store.editProblem(s(u"../outside")), s(u"there is no such playlist"));
}

QTEST_GUILESS_MAIN(PlaylistsEditTest)
#include "tst_playlists_edit.moc"
