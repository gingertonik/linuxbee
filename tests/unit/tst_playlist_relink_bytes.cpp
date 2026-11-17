// Unit tests added by the builder of fix 59 (not locked). Expected bytes are written out by hand
// from the rule in docs/spec/playlists.md (Y-RELINK) and docs/spec/library.md (L-MOVED):
// every line that does not name a moved path stays byte for byte as it was.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "library/library.h"
#include "playlists/playliststore.h"

using namespace linuxbee;

namespace {

using Moves = QHash<QString, QString>;

QByteArray bytesOf(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<unreadable>");
}

void write(const QString &path, const QByteArray &data)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QCOMPARE(f.write(data), data.size());
}

}

class PlaylistRelinkBytesTest : public QObject {
    Q_OBJECT
private slots:
    void otherLinesKeepTheirBytes();
    void aFileWithoutAMovedPathIsNotWritten();
    void aTrackMovedTwiceInOneCallIsOneMove();
};

void PlaylistRelinkBytesTest::otherLinesKeepTheirBytes()
{
    QTemporaryDir dir;
    PlaylistStore store(dir.path() + QStringLiteral("/lists"));
    const QString id = store.create(QStringLiteral("Mix"));
    QVERIFY(!id.isEmpty());
    const QString file = dir.path() + QStringLiteral("/lists/Mix.m3u8");
    // A byte order mark, CRLF ends, a blank line, a line that is not valid UTF-8, a comment line,
    // one moved path twice, and no end of line after the last entry.
    const QByteArray before = QByteArray("\xEF\xBB\xBF") + "/m/one\r\n\r\n/m/bad\xFF\xFE\r\n# keep me\r\n/m/two\r\n/m/one\r\n/m/last";
    const QByteArray after = QByteArray("\xEF\xBB\xBF") + "/n/uno\r\n\r\n/m/bad\xFF\xFE\r\n# keep me\r\n/m/two\r\n/n/uno\r\n/m/last";
    write(file, before);
    QCOMPARE(store.relink({ { QStringLiteral("/m/one"), QStringLiteral("/n/uno") } }), 1);
    QCOMPARE(bytesOf(file), after);
}

void PlaylistRelinkBytesTest::aFileWithoutAMovedPathIsNotWritten()
{
    QTemporaryDir dir;
    PlaylistStore store(dir.path() + QStringLiteral("/lists"));
    const QString id = store.create(QStringLiteral("Quiet"));
    QVERIFY(!id.isEmpty());
    const QString file = dir.path() + QStringLiteral("/lists/Quiet.m3u8");
    const QByteArray before = "/m/a\r\n\r\n/m/b";
    write(file, before);
    const QDateTime time = QFileInfo(file).lastModified();
    QTest::qWait(1100);
    QSignalSpy changed(&store, &PlaylistStore::tracksChanged);
    QCOMPARE(store.relink({ { QStringLiteral("/m/zzz"), QStringLiteral("/n/zzz") } }), 0);
    QCOMPARE(changed.count(), 0);
    QCOMPARE(bytesOf(file), before);
    QCOMPARE(QFileInfo(file).lastModified(), time);
}

void PlaylistRelinkBytesTest::aTrackMovedTwiceInOneCallIsOneMove()
{
    // L-MOVED: recordMoves follows a track through two moves in one call; the signal tells where the track was before
    // the call and where it ended.
    QTemporaryDir dir;
    auto library = Library::open(QStringLiteral(":memory:"));
    QVERIFY(library);
    const QString a = dir.path() + QStringLiteral("/a.flac");
    QVERIFY(QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/basic.flac"), a));
    const qint64 id = library->addFile(a, QStringLiteral("Music"), QDateTime::currentDateTimeUtc());
    QVERIFY(id != 0);
    QSignalSpy moved(library.get(), &Library::pathsMoved);
    QSignalSpy updated(library.get(), &Library::tracksUpdated);
    const QString x = dir.path() + QStringLiteral("/x.flac");
    const QString y = dir.path() + QStringLiteral("/y.flac");
    QCOMPARE(library->recordMoves({ { a, x }, { x, y } }), 1);
    QCOMPARE(moved.count(), 1);
    const Moves expected { { a, y } };
    const Moves got = moved.at(0).at(0).value<Moves>();
    QCOMPARE(got, expected);
    QCOMPARE(updated.count(), 1);
    QCOMPARE(updated.at(0).at(0).value<QList<qint64>>(), QList<qint64>({ id }));
    // Away and back to where it started in one call: nothing moved for the playlists.
    QCOMPARE(library->recordMoves({ { y, x }, { x, y } }), 0);
    QCOMPARE(moved.count(), 1);
}

QTEST_MAIN(PlaylistRelinkBytesTest)
#include "tst_playlist_relink_bytes.moc"
