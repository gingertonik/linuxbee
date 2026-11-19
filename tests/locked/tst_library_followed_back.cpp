// LOCKED ACCEPTANCE TEST (a scan follows a moved file: a row whose own file is back is no row a
// file may be). Written by the reviewer, not the builder. The builder makes these pass by changing
// src/, never by editing this file. A test believed to be wrong is reported, with the reason, and
// left alone.
//
// Expected values are L-FOLLOW in docs/spec/library.md ("at whose path nothing is"), applied to
// copies of the fixtures in tests/data/tags/. The fault this holds shut (the review of that
// rule's first build, 2026-11-19): a track marked missing by an earlier scan, whose file was back
// at its own path, was a row a file may be. A copy alike in size, time and tags, found in the same
// scan, took the row: the plays went to the copy, the playlists named the copy, and the file that
// had been the track all along came back as a new one with nothing. It depended on the size of
// the parts.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

#include <algorithm>

#include "library/library.h"

using namespace linuxbee;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

const QDateTime kNow = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);
const QDateTime kLater = QDateTime(QDate(2026, 12, 1), QTime(8, 30), QTimeZone::UTC);

QDateTime timeOf(const QString &path)
{
    return QFileInfo(path).fileTime(QFileDevice::FileModificationTime);
}

// The file's contents are given the time `when`, as a copy that keeps the times does.
bool setTime(const QString &path, const QDateTime &when)
{
    QFile file(path);
    return file.open(QIODevice::ReadWrite)
        && file.setFileTime(when, QFileDevice::FileModificationTime);
}

// A music folder in a temporary place:
//   a/one.mp3  a/two.flac  b/three.m4a  b/four.ogg  b/notaudio.mp3  c/five.opus
struct Folder {
    QTemporaryDir dir;
    QString root;
    Folder()
    {
        root
            = QDir::cleanPath(QFileInfo(dir.path()).canonicalFilePath()) + QStringLiteral("/music");
        for (const char16_t *sub : { u"/a", u"/b", u"/c" })
            QDir().mkpath(root + s(sub));
        put(u"basic.mp3", u"a/one.mp3");
        put(u"basic.flac", u"a/two.flac");
        put(u"basic.m4a", u"b/three.m4a");
        put(u"basic.ogg", u"b/four.ogg");
        put(u"notaudio.mp3", u"b/notaudio.mp3");
        put(u"basic.opus", u"c/five.opus");
    }
    QString path(const char16_t *relative) const { return root + QLatin1Char('/') + s(relative); }
    void put(const char16_t *fixtureName, const char16_t *relative)
    {
        QFile::remove(path(relative));
        if (!QFile::copy(
                QStringLiteral(LINUXBEE_TEST_DATA "/tags/") + s(fixtureName), path(relative)))
            QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
    }
    // Another program moves a file: its bytes and its time go with it.
    bool move(const char16_t *from, const char16_t *to) const
    {
        return QFile::rename(path(from), path(to));
    }
};

ScanOptions options(const QDateTime &now = kLater)
{
    ScanOptions o;
    o.category = QStringLiteral("Inbox"); // not the section the tracks are in
    o.now = now;
    return o;
}

QString ids(const QList<qint64> &list)
{
    QStringList parts;
    for (const qint64 id : list)
        parts.append(QString::number(id));
    return parts.join(QLatin1Char(' '));
}

// One library that has scanned the folder at kNow into Music, with one.mp3 played three times
// and skipped once.
struct Scanned {
    Folder f;
    std::unique_ptr<Library> library = Library::open(QStringLiteral(":memory:"));
    qint64 one = 0;
    Scanned()
    {
        ScanOptions first;
        first.now = kNow;
        if (library->scan({ f.root }, first).added.size() != 5)
            QTest::qFail("the fixtures did not scan", __FILE__, __LINE__);
        one = id(u"a/one.mp3");
        for (int i = 1; i <= 3; ++i)
            library->recordPlay(one, kNow.addSecs(60 * i));
        library->recordSkip(one, kNow.addSecs(600));
    }
    qint64 id(const char16_t *relative) const
    {
        const auto t = library->trackByPath(f.path(relative));
        return t ? t->id : 0;
    }
    ScanFindings look(const char16_t *folder = u"") const
    {
        return findForScan(
            { s(folder).isEmpty() ? f.root : f.path(folder) }, library->scanKnown(), { });
    }
    // A scan of the whole folder: in one, or in parts of `partSize` files. Fails the test when
    // a part that is not the last carries a file that was held back.
    ScanReport scan(int partSize = 0)
    {
        if (partSize == 0)
            return library->scan({ f.root }, options());
        ScanReport sum;
        ScanFindOptions find;
        find.partSize = partSize;
        find.part = [this, &sum](const ScanFindings &part) {
            if (!part.maybeMoved.isEmpty())
                QTest::qFail("an early part carries a held-back file", __FILE__, __LINE__);
            sum = addedUp(sum, library->applyScan(part, options()));
        };
        const ScanFindings rest = findForScan({ f.root }, library->scanKnown(), find);
        return addedUp(sum, library->applyScan(rest, options()));
    }
    QStringList pathsOf(const QList<qint64> &list) const
    {
        QStringList paths;
        for (const qint64 id : list) {
            const auto t = library->track(id);
            paths.append(t ? t->path : s(u"<no such track>"));
        }
        return paths;
    }
};

}

class LibraryFollowedBackTest : public QObject {
    Q_OBJECT
private slots:
    void aMissingRowWhoseFileIsBackIsFound_data();
    void aMissingRowWhoseFileIsBackIsFound();
    void theLookingHoldsNothingBackForIt();
    void onlyTheFolderOfTheCopyIsScanned();
    void theFileIsBackByTheStoring();
};

namespace {

// a/one.mp3 goes away, a scan marks its track missing, and the file is back where it was.
bool awayMarkedAndBack(Scanned &x)
{
    const QString own = x.f.path(u"a/one.mp3");
    const QString aside = x.f.dir.path() + s(u"/aside.mp3");
    if (!QFile::rename(own, aside))
        return false;
    if (x.scan().nowMissing != QList<qint64> { x.one } || !x.library->track(x.one)->missing)
        return false;
    return QFile::rename(aside, own);
}

// A copy of a/one.mp3 that keeps its time, as `cp -p` makes one.
bool copyOfOne(Scanned &x, const QString &to)
{
    const QString own = x.f.path(u"a/one.mp3");
    return QFile::copy(own, to) && setTime(to, timeOf(own));
}

}

void LibraryFollowedBackTest::aMissingRowWhoseFileIsBackIsFound_data()
{
    QTest::addColumn<int>("partSize");
    QTest::addColumn<QString>("copy");
    const QString before = s(u"a/a-copy.mp3"); // looked at before a/one.mp3
    const QString after = s(u"c/zzz.mp3"); // and after it
    QTest::newRow("a scan in one, the copy first") << 0 << before;
    QTest::newRow("a scan in one, the copy last") << 0 << after;
    QTest::newRow("in parts of one file, the copy first") << 1 << before;
    QTest::newRow("in parts of one file, the copy last") << 1 << after;
    QTest::newRow("in parts of four files, the copy first") << 4 << before;
    QTest::newRow("in parts of four files, the copy last") << 4 << after;
}

void LibraryFollowedBackTest::aMissingRowWhoseFileIsBackIsFound()
{
    // The track's own file is back, and a copy of it lies elsewhere. The row is found again,
    // as before this rule; the copy is a new track. In one call or in parts, whichever of the
    // two is looked at first.
    QFETCH(int, partSize);
    QFETCH(QString, copy);
    Scanned x;
    QVERIFY(awayMarkedAndBack(x));
    const QString other = x.f.root + QLatin1Char('/') + copy;
    QVERIFY(copyOfOne(x, other));
    const ScanReport r = x.scan(partSize);
    QVERIFY2(r.refused.isEmpty(), qPrintable(r.refused));
    QCOMPARE(ids(r.followed), QString());
    QCOMPARE(r.found, QList<qint64> { x.one });
    QCOMPARE(x.pathsOf(r.added), QStringList({ other }));
    QCOMPARE(ids(r.nowMissing), QString());
    const auto row = x.library->track(x.one);
    QVERIFY(row.has_value());
    QCOMPARE(row->path, x.f.path(u"a/one.mp3"));
    QVERIFY(!row->missing);
    QCOMPARE(row->playCount, 3);
    QCOMPARE(row->skipCount, 1);
    QCOMPARE(row->dateAdded, kNow);
    QCOMPARE(x.library->track(r.added.value(0))->playCount, 0);
    QCOMPARE(x.library->count(), 6);
    // A scan after it finds nothing to do.
    const ScanReport again = x.scan(partSize);
    QCOMPARE(again.unchanged, 6);
    QVERIFY(again.followed.isEmpty() && again.added.isEmpty() && again.found.isEmpty());
}

void LibraryFollowedBackTest::theLookingHoldsNothingBackForIt()
{
    // L-FOLLOW, the looking: a track marked missing at whose path a file is, is no row a file
    // may be. The copy is not held back; it is a new file as any other.
    Scanned x;
    QVERIFY(awayMarkedAndBack(x));
    const QString other = x.f.path(u"c/zzz.mp3");
    QVERIFY(copyOfOne(x, other));
    const ScanFindings found = x.look();
    QCOMPARE(found.maybeMoved.size(), 0);
    QCOMPARE(found.found, QList<qint64> { x.one });
    QStringList fresh;
    for (const LibraryTrack &reading : found.fresh)
        fresh.append(reading.path);
    QCOMPARE(fresh, QStringList({ other }));
}

void LibraryFollowedBackTest::onlyTheFolderOfTheCopyIsScanned()
{
    // The row's own folder is not scanned this time, so nothing finds the row again. Its file
    // is there all the same, and that is asked of the disk: the copy does not take the row.
    Scanned x;
    QVERIFY(awayMarkedAndBack(x));
    const QString other = x.f.path(u"c/zzz.mp3");
    QVERIFY(copyOfOne(x, other));
    const ScanReport r = x.library->scan({ x.f.path(u"c") }, options());
    QCOMPARE(ids(r.followed), QString());
    QCOMPARE(ids(r.found), QString());
    QCOMPARE(x.pathsOf(r.added), QStringList({ other }));
    auto row = x.library->track(x.one);
    QCOMPARE(row->path, x.f.path(u"a/one.mp3"));
    QVERIFY(row->missing); // until its folder is scanned
    QCOMPARE(row->playCount, 3);
    // The whole folder: the row is found again, with all it had.
    const ScanReport whole = x.scan();
    QCOMPARE(whole.found, QList<qint64> { x.one });
    row = x.library->track(x.one);
    QCOMPARE(row->path, x.f.path(u"a/one.mp3"));
    QVERIFY(!row->missing);
    QCOMPARE(row->playCount, 3);
    QCOMPARE(x.library->count(), 6);
}

void LibraryFollowedBackTest::theFileIsBackByTheStoring()
{
    // L-FOLLOW, the storing: the file was moved, the looking held the moved file back for the
    // row, and before the storing a file is at the row's own path again. The row is not moved
    // away from a file that is there: the moved file is a new track.
    Scanned x;
    const QString own = x.f.path(u"a/one.mp3");
    const QString moved = x.f.path(u"b/uno.mp3");
    QVERIFY(x.f.move(u"a/one.mp3", u"b/uno.mp3"));
    const ScanFindings found = x.look();
    QCOMPARE(found.maybeMoved.size(), 1);
    QVERIFY(QFile::copy(moved, own) && setTime(own, timeOf(moved)));
    const ScanReport r = x.library->applyScan(found, options());
    QVERIFY2(r.refused.isEmpty(), qPrintable(r.refused));
    QCOMPARE(ids(r.followed), QString());
    QCOMPARE(x.pathsOf(r.added), QStringList({ moved }));
    auto row = x.library->track(x.one);
    QVERIFY(row.has_value());
    QCOMPARE(row->path, own);
    QCOMPARE(row->playCount, 3);
    // The next scan has the row at its own file, not missing, and nothing more to add.
    const ScanReport next = x.scan();
    QCOMPARE(ids(next.followed), QString());
    QCOMPARE(ids(next.added), QString());
    row = x.library->track(x.one);
    QCOMPARE(row->path, own);
    QVERIFY(!row->missing);
    QCOMPARE(row->playCount, 3);
    QCOMPARE(x.library->count(), 6);
}

QTEST_GUILESS_MAIN(LibraryFollowedBackTest)
#include "tst_library_followed_back.moc"
