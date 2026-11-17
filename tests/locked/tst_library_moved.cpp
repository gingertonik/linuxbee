// LOCKED ACCEPTANCE TEST (a file the program moved keeps its place: the library says so, playlists and the queue follow). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/library.md (L-MOVED), docs/spec/playlists.md (Y-RELINK) and
// docs/spec/play-queue.md (Q-RENAME). Core only; copies of fixtures in a temporary folder.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

#include "library/library.h"
#include "playback/playqueue.h"
#include "playlists/playliststore.h"

using namespace linuxbee;

namespace {

using Moves = QHash<QString, QString>;

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

const QDateTime kNow = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);

struct Box {
    QTemporaryDir dir;
    std::unique_ptr<Library> library;
    QString a, b, c;
    qint64 ia = 0, ib = 0, ic = 0;

    Box()
        : library(Library::open(QStringLiteral(":memory:")))
    {
        QDir(dir.path()).mkpath(QStringLiteral("music/sub"));
        QDir(dir.path()).mkpath(QStringLiteral("lists"));
        a = put("basic.flac", "music/a.flac");
        b = put("basic.mp3", "music/b.mp3");
        c = put("basic.ogg", "music/sub/c.ogg");
        ia = library->addFile(a, QStringLiteral("Music"), kNow);
        ib = library->addFile(b, QStringLiteral("Music"), kNow);
        ic = library->addFile(c, QStringLiteral("Music"), kNow);
        if (ia == 0 || ib == 0 || ic == 0)
            QTest::qFail("a fixture did not go into the library", __FILE__, __LINE__);
    }
    QString put(const char *from, const char *to)
    {
        const QString target = dir.path() + QLatin1Char('/') + QLatin1String(to);
        if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/") + QLatin1String(from), target))
            QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
        return target;
    }
    QString at(const char *name) const { return dir.path() + QLatin1Char('/') + QLatin1String(name); }
};

Moves movesOf(const QSignalSpy &spy, int i)
{
    return spy.at(i).at(0).value<Moves>();
}

QByteArray bytesOf(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<unreadable>");
}

}

class LibraryMovedTest : public QObject {
    Q_OBJECT
private slots:
    void theLibrarySaysWhatMoved();
    void manyAtOnceIsOneSignal();
    void aRemappedFolder();
    void playlistsFollow();
    void theQueueFollows();
};

void LibraryMovedTest::theLibrarySaysWhatMoved()
{
    // L-MOVED: setPath that changes the path emits pathsMoved once, old to new, after tracksUpdated's change is stored.
    Box box;
    QSignalSpy moved(box.library.get(), &Library::pathsMoved);
    QVERIFY(QFile::rename(box.a, box.at("music/z.flac")));
    QVERIFY(box.library->setPath(box.ia, box.at("music/z.flac")));
    QCOMPARE(moved.count(), 1);
    QCOMPARE(movesOf(moved, 0), Moves({ { box.a, box.at("music/z.flac") } }));
    // Nothing moved: the same path again, an unknown id, a path another track has.
    QVERIFY(box.library->setPath(box.ia, box.at("music/z.flac")));
    QVERIFY(!box.library->setPath(987654, box.at("music/q.flac")));
    QVERIFY(!box.library->setPath(box.ia, box.b));
    QCOMPARE(moved.count(), 1);
}

void LibraryMovedTest::manyAtOnceIsOneSignal()
{
    // L-MOVED: setPaths is setPath for each, with one tracksUpdated and one pathsMoved for the call.
    Box box;
    QSignalSpy moved(box.library.get(), &Library::pathsMoved);
    QSignalSpy updated(box.library.get(), &Library::tracksUpdated);
    QVERIFY(QFile::rename(box.a, box.at("music/a2.flac")));
    QVERIFY(QFile::rename(box.c, box.at("music/c2.ogg")));
    const int n = box.library->setPaths({
        { box.ia, box.at("music/a2.flac") },
        { box.ib, box.at("music/a2.flac") }, // taken by the one before: left alone
        { 987654, box.at("music/x.flac") }, // unknown
        { box.ic, box.at("music/c2.ogg") },
    });
    QCOMPARE(n, 2);
    QCOMPARE(moved.count(), 1);
    QCOMPARE(movesOf(moved, 0), Moves({ { box.a, box.at("music/a2.flac") }, { box.c, box.at("music/c2.ogg") } }));
    QCOMPARE(updated.count(), 1);
    QCOMPARE(updated.at(0).at(0).value<QList<qint64>>().size(), 2);
    QCOMPARE(box.library->track(box.ia)->path, box.at("music/a2.flac"));
    QCOMPARE(box.library->track(box.ib)->path, box.b);
    QCOMPARE(box.library->track(box.ic)->path, box.at("music/c2.ogg"));
    QVERIFY(!box.library->track(box.ic)->missing);
    // Nothing to do: no signal.
    QCOMPARE(box.library->setPaths({ }), 0);
    QCOMPARE(box.library->setPaths({ { box.ia, box.at("music/a2.flac") } }), 0);
    QCOMPARE(moved.count(), 1);
    QCOMPARE(updated.count(), 1);
}

void LibraryMovedTest::aRemappedFolder()
{
    // L-MOVED: relinkPaths emits one pathsMoved with every path it changed.
    Box box;
    QSignalSpy moved(box.library.get(), &Library::pathsMoved);
    QCOMPARE(box.library->relinkPaths(box.at("music"), box.at("elsewhere")), 3);
    QCOMPARE(moved.count(), 1);
    QCOMPARE(movesOf(moved, 0), Moves({
                                    { box.a, box.at("elsewhere/a.flac") },
                                    { box.b, box.at("elsewhere/b.mp3") },
                                    { box.c, box.at("elsewhere/sub/c.ogg") },
                                }));
    QCOMPARE(box.library->relinkPaths(box.at("nowhere"), box.at("x")), 0);
    QCOMPARE(moved.count(), 1);
}

void LibraryMovedTest::playlistsFollow()
{
    // Y-RELINK: every entry that names an old path names the new one; place and repeats are kept;
    // a playlist that names none is not written.
    Box box;
    PlaylistStore store(box.at("lists"));
    const QString mix = store.create(s(u"Mix"));
    const QString other = store.create(s(u"Other"));
    const QString inFolder = store.create(s(u"Deep"), s(u"Rock"));
    const QString gone = box.at("music/not-in-the-library.mp3");
    QVERIFY(store.setTracks(mix, { box.a, box.b, gone, box.a, box.c }));
    QVERIFY(store.setTracks(other, { box.b }));
    QVERIFY(store.setTracks(inFolder, { box.c, box.a }));
    const QString otherFile = box.at("lists/Other.m3u8");
    QVERIFY(QFile::exists(otherFile));
    const QByteArray otherBytes = bytesOf(otherFile);
    const QDateTime otherTime = QFileInfo(otherFile).lastModified();
    QTest::qWait(1100); // so that a rewrite would show in the file's time
    QSignalSpy changed(&store, &PlaylistStore::tracksChanged);

    const Moves moves { { box.a, box.at("new/A.flac") }, { box.c, box.at("new/C.ogg") }, { gone, box.at("new/G.mp3") } };
    QCOMPARE(store.relink(moves), 2);
    QCOMPARE(store.tracks(mix), QStringList({ box.at("new/A.flac"), box.b, box.at("new/G.mp3"), box.at("new/A.flac"), box.at("new/C.ogg") }));
    QCOMPARE(store.tracks(inFolder), QStringList({ box.at("new/C.ogg"), box.at("new/A.flac") }));
    QCOMPARE(store.tracks(other), QStringList({ box.b }));
    QCOMPARE(bytesOf(otherFile), otherBytes);
    QCOMPARE(QFileInfo(otherFile).lastModified(), otherTime);
    QCOMPARE(changed.count(), 2); // one for each playlist written
    // A swap is a swap: every entry is looked up once, in the list as it was.
    QVERIFY(store.setTracks(other, { box.b, box.at("new/A.flac") }));
    QCOMPARE(store.relink({ { box.b, box.at("new/A.flac") }, { box.at("new/A.flac"), box.b } }), 3);
    QCOMPARE(store.tracks(other), QStringList({ box.at("new/A.flac"), box.b }));
    // Nothing to do.
    QCOMPARE(store.relink({ }), 0);
    QCOMPARE(store.relink({ { box.at("x"), box.at("y") } }), 0);
}

void LibraryMovedTest::theQueueFollows()
{
    // Q-RENAME: entries are renamed where they are. Ids, order, the current entry and the shuffle round stay.
    PlayQueue q;
    q.playNow({ s(u"/m/1"), s(u"/m/2"), s(u"/m/3"), s(u"/m/2"), s(u"/m/4") }, 1);
    q.setShuffle(true, 7);
    const QList<PlayQueue::Entry> before = q.entries();
    const int current = q.currentIndex();
    QSignalSpy entries(&q, &PlayQueue::entriesChanged);
    QSignalSpy cur(&q, &PlayQueue::currentChanged);
    QCOMPARE(q.renamePaths({ { s(u"/m/2"), s(u"/n/two") }, { s(u"/m/4"), s(u"/n/four") }, { s(u"/m/9"), s(u"/n/nine") } }), 3);
    QCOMPARE(entries.count(), 1);
    QCOMPARE(cur.count(), 0);
    const QList<PlayQueue::Entry> after = q.entries();
    QCOMPARE(after.size(), before.size());
    QStringList paths;
    for (int i = 0; i < after.size(); ++i) {
        QCOMPARE(after.at(i).entryId, before.at(i).entryId);
        paths.append(after.at(i).path);
    }
    QCOMPARE(paths, QStringList({ s(u"/m/1"), s(u"/n/two"), s(u"/m/3"), s(u"/n/two"), s(u"/n/four") }));
    QCOMPARE(q.currentIndex(), current);
    QCOMPARE(q.currentPath(), s(u"/n/two"));
    // The round goes on: every other entry is still played once before the list ends.
    QSet<qint64> played { after.at(q.currentIndex()).entryId };
    while (q.next(true) >= 0)
        played.insert(q.entries().at(q.currentIndex()).entryId);
    QCOMPARE(played.size(), 5);
    // Nothing to do: no signal.
    QCOMPARE(q.renamePaths({ { s(u"/m/9"), s(u"/n/nine") } }), 0);
    QCOMPARE(q.renamePaths({ }), 0);
}

QTEST_MAIN(LibraryMovedTest)
#include "tst_library_moved.moc"
