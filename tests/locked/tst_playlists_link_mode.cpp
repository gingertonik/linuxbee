// LOCKED ACCEPTANCE TEST (the playlist store: a playlist that is a link, and one that is
// read-only). Written by the reviewer, not the builder. The builder makes these pass by changing
// src/, never by editing this file. A test believed to be wrong is reported, with the reason, and
// left alone.
//
// Expected values are Y-EDIT ("A link to a file is the file") and Y-SAME in docs/spec/playlists.md.
// The faults this holds shut (the write-path re-probe, 2026-11-22, N1 and N2; both came with
// fix-100, which put a spare name and a rename where QSaveFile had been): the first edit of a
// playlist that is a link replaced the link with a file, and the file it led to kept the old list;
// and a read-only playlist was edited. The files are real, in a temporary folder.
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <sys/stat.h>
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

bool put(const QString &path, const QByteArray &data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(data) == data.size();
}

QStringList hidden(const QString &folder)
{
    return QDir(folder).entryList(
        QStringList { s(u".lb*") }, QDir::Files | QDir::Hidden | QDir::System);
}

int modeOf(const QString &path)
{
    struct stat info { };
    return ::stat(QFile::encodeName(path).constData(), &info) == 0 ? int(info.st_mode & 07777) : -1;
}

const QByteArray kTwo = QByteArrayLiteral("/m/a.mp3\n/m/b.mp3\n");

}

class PlaylistsLinkModeTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void aLinkedPlaylistIsWrittenThroughItsLink();
    void everyEditGoesThroughTheLink();
    void aReadOnlyPlaylistIsNotEdited();
    void aLinkToAReadOnlyPlaylist();

private:
    QTemporaryDir m_dir;
    int m_n = 0;
    QString m_store;
    QString m_shared;
    QString link(const QByteArray &data, int mode = 0644);
};

void PlaylistsLinkModeTest::init()
{
    QVERIFY(m_dir.isValid());
    const QString root = m_dir.filePath(QStringLiteral("t%1").arg(++m_n));
    m_store = root + s(u"/store");
    m_shared = root + s(u"/shared");
    QVERIFY(QDir().mkpath(m_store));
    QVERIFY(QDir().mkpath(m_shared));
}

// store/Shared.m3u8 is a link to shared/Shared.m3u8, which holds `data`.
QString PlaylistsLinkModeTest::link(const QByteArray &data, int mode)
{
    const QString target = m_shared + s(u"/Shared.m3u8");
    if (!put(target, data) || ::chmod(QFile::encodeName(target).constData(), mode_t(mode)) != 0
        || !QFile::link(target, m_store + s(u"/Shared.m3u8")))
        QTest::qFail("could not make the link", __FILE__, __LINE__);
    return target;
}

void PlaylistsLinkModeTest::aLinkedPlaylistIsWrittenThroughItsLink()
{
    // Y-EDIT: a link to a file is the file. The edit goes to the file the link leads to; the link
    // stays a link, to the same file; nothing is left beside either.
    const QString target = link(QByteArrayLiteral("/m/a.mp3\n"), 0640);
    PlaylistStore store(m_store);
    QSignalSpy changed(&store, &PlaylistStore::tracksChanged);
    QCOMPARE(store.append(s(u"Shared"), { s(u"/m/b.mp3") }), 1);
    const QFileInfo name(m_store + s(u"/Shared.m3u8"));
    QVERIFY2(name.isSymLink(), "the link was replaced by a file");
    QCOMPARE(name.symLinkTarget(), target);
    QCOMPARE(bytesOf(target), kTwo);
    QCOMPARE(modeOf(target), 0640);
    QCOMPARE(store.tracks(s(u"Shared")), QStringList({ s(u"/m/a.mp3"), s(u"/m/b.mp3") }));
    QCOMPARE(hidden(m_store), QStringList());
    QCOMPARE(hidden(m_shared), QStringList());
    QCOMPARE(changed.count(), 1);
}

void PlaylistsLinkModeTest::everyEditGoesThroughTheLink()
{
    // The same for removeAt, moveTrack, setTracks and relink.
    const QString target = link(kTwo);
    PlaylistStore store(m_store);
    const QString l = m_store + s(u"/Shared.m3u8");
    QVERIFY(store.moveTrack(s(u"Shared"), 1, 0));
    QVERIFY(QFileInfo(l).isSymLink());
    QCOMPARE(bytesOf(target), QByteArrayLiteral("/m/b.mp3\n/m/a.mp3\n"));
    QCOMPARE(store.removeAt(s(u"Shared"), { 0 }), 1);
    QVERIFY(QFileInfo(l).isSymLink());
    QCOMPARE(bytesOf(target), QByteArrayLiteral("/m/a.mp3\n"));
    QVERIFY(store.setTracks(s(u"Shared"), { s(u"/m/c.mp3") }));
    QVERIFY(QFileInfo(l).isSymLink());
    QCOMPARE(bytesOf(target), QByteArrayLiteral("/m/c.mp3\n"));
    QCOMPARE(store.relink({ { s(u"/m/c.mp3"), s(u"/n/c.mp3") } }), 1);
    QVERIFY(QFileInfo(l).isSymLink());
    QCOMPARE(bytesOf(target), QByteArrayLiteral("/n/c.mp3\n"));
    QCOMPARE(hidden(m_store), QStringList());
    QCOMPARE(hidden(m_shared), QStringList());
}

void PlaylistsLinkModeTest::aReadOnlyPlaylistIsNotEdited()
{
    // Y-EDIT: a playlist that cannot be written to (mode 0444) is not edited, by any of the five
    // calls: each does nothing and says the file could not be written. It is shown as it is.
    if (::geteuid() == 0)
        QSKIP("this user may write to a read-only file");
    const QString file = m_store + s(u"/RO.m3u8");
    QVERIFY(put(file, kTwo));
    QVERIFY(::chmod(QFile::encodeName(file).constData(), 0444) == 0);
    PlaylistStore store(m_store);
    QSignalSpy changed(&store, &PlaylistStore::tracksChanged);
    const QString words = s(u"the file could not be written");
    QCOMPARE(store.append(s(u"RO"), { s(u"/m/c.mp3") }), 0);
    QCOMPARE(store.lastEditProblem(), words);
    QCOMPARE(store.removeAt(s(u"RO"), { 0 }), 0);
    QCOMPARE(store.lastEditProblem(), words);
    QVERIFY(!store.moveTrack(s(u"RO"), 1, 0));
    QCOMPARE(store.lastEditProblem(), words);
    QVERIFY(!store.setTracks(s(u"RO"), { s(u"/m/c.mp3") }));
    QCOMPARE(store.lastEditProblem(), words);
    QCOMPARE(store.relink({ { s(u"/m/a.mp3"), s(u"/n/a.mp3") } }), 0);
    QCOMPARE(store.relinkPassedOver(), QStringList({ s(u"RO") }));
    QCOMPARE(bytesOf(file), kTwo);
    QCOMPARE(modeOf(file), 0444);
    QCOMPARE(store.tracks(s(u"RO")), QStringList({ s(u"/m/a.mp3"), s(u"/m/b.mp3") }));
    QCOMPARE(changed.count(), 0);
    QCOMPARE(hidden(m_store), QStringList());
}

void PlaylistsLinkModeTest::aLinkToAReadOnlyPlaylist()
{
    // Both together: the file the link leads to is read-only. Nothing is written, and the link
    // stays.
    if (::geteuid() == 0)
        QSKIP("this user may write to a read-only file");
    const QString target = link(kTwo, 0444);
    PlaylistStore store(m_store);
    QCOMPARE(store.append(s(u"Shared"), { s(u"/m/c.mp3") }), 0);
    QCOMPARE(store.lastEditProblem(), s(u"the file could not be written"));
    QVERIFY(QFileInfo(m_store + s(u"/Shared.m3u8")).isSymLink());
    QCOMPARE(bytesOf(target), kTwo);
    QCOMPARE(hidden(m_store), QStringList());
    QCOMPARE(hidden(m_shared), QStringList());
}

QTEST_APPLESS_MAIN(PlaylistsLinkModeTest)
#include "tst_playlists_link_mode.moc"
