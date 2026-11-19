// LOCKED ACCEPTANCE TEST (a scan follows a file that another program moved). Written by the
// reviewer, not the builder. The builder makes these pass by changing src/, never by editing this
// file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are L-FOLLOW in docs/spec/library.md, applied to copies of the fixtures in
// tests/data/tags/. The fault this holds shut (the write-path audit, part 5, F1, 2026-11-18): a
// file renamed or moved by any other hand was a track gone and a track new. The plays, the skips,
// the last play and the date added stayed on a row marked missing, and nothing in the program
// could bring them to the file.
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

QByteArray bytesOf(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArrayLiteral("<cannot open>");
    return file.readAll();
}

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

// Every signal of the library since this was made, in the order they came.
struct Told {
    QObject context;
    QStringList lines;
    explicit Told(Library *library)
    {
        QObject::connect(library, &Library::tracksAdded, &context,
            [this](const QList<qint64> &list) { lines.append(s(u"added ") + ids(list)); });
        QObject::connect(library, &Library::tracksUpdated, &context,
            [this](const QList<qint64> &list) { lines.append(s(u"updated ") + ids(list)); });
        QObject::connect(library, &Library::tracksRemoved, &context,
            [this](const QList<qint64> &list) { lines.append(s(u"removed ") + ids(list)); });
        QObject::connect(library, &Library::failed, &context,
            [this](const QString &what) { lines.append(s(u"failed ") + what); });
        QObject::connect(library, &Library::pathsMoved, &context,
            [this](const QHash<QString, QString> &oldToNew) {
                QStringList moves;
                for (auto move = oldToNew.cbegin(); move != oldToNew.cend(); ++move)
                    moves.append(move.key() + s(u" > ") + move.value());
                moves.sort();
                lines.append(s(u"moved ") + moves.join(s(u" | ")));
            });
    }
};

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

class LibraryFollowedTest : public QObject {
    Q_OBJECT
private slots:
    void aFileThatWasMovedKeepsItsRow_data();
    void aFileThatWasMovedKeepsItsRow();
    void aTrackMarkedMissingIsFollowed_data();
    void aTrackMarkedMissingIsFollowed();
    void twoAlikeFilesAndOneRow_data();
    void twoAlikeFilesAndOneRow();
    void twoAlikeRowsAndOneFile();
    void theSameSizeAndTimeAndOtherTags();
    void whatIsHeldBack();
    void theRowIsGoneByTheStoring();
    void thePathIsHeldByTheStoring();
    void theRowIsElsewhereByTheStoring();
    void aCancelledScanStoresNothingForIt();
    void theOrderOfTheSignal();
    void addedUpAddsThem();
};

void LibraryFollowedTest::aFileThatWasMovedKeepsItsRow_data()
{
    QTest::addColumn<int>("partSize");
    QTest::newRow("a scan in one") << 0;
    QTest::newRow("in parts of one file") << 1;
    QTest::newRow("in parts of four files") << 4;
}

void LibraryFollowedTest::aFileThatWasMovedKeepsItsRow()
{
    // Another program renames a/one.mp3 to b/uno.mp3. The next scan does not see a track gone
    // and a track new: the row is at the new path, with its id and everything the library
    // alone knows of it, and the library says the path moved.
    QFETCH(int, partSize);
    Scanned x;
    QVERIFY(x.one > 0);
    const LibraryTrack before = *x.library->track(x.one);
    QCOMPARE(before.playCount, 3);
    const QString from = x.f.path(u"a/one.mp3");
    const QString to = x.f.path(u"b/uno.mp3");
    QVERIFY(x.f.move(u"a/one.mp3", u"b/uno.mp3"));
    Told told(x.library.get());
    const ScanReport r = x.scan(partSize);
    QCOMPARE(r.followed, QList<qint64> { x.one });
    QCOMPARE(ids(r.added), QString());
    QCOMPARE(ids(r.updated), QString());
    QCOMPARE(ids(r.nowMissing), QString());
    QCOMPARE(ids(r.found), QString());
    QCOMPARE(r.unchanged, 4);
    QCOMPARE(r.passedOver, 0);
    QVERIFY2(r.refused.isEmpty(), qPrintable(r.refused));
    QCOMPARE(x.library->count(), 5);
    QVERIFY(!x.library->trackByPath(from));
    const auto now = x.library->track(x.one);
    QVERIFY(now.has_value());
    QCOMPARE(now->path, to);
    QVERIFY(!now->missing);
    QCOMPARE(now->playCount, 3);
    QCOMPARE(now->skipCount, 1);
    QCOMPARE(now->lastPlayed, before.lastPlayed);
    QCOMPARE(now->dateAdded, kNow);
    QCOMPARE(now->category, s(u"Music"));
    QCOMPARE(now->tags.fields, before.tags.fields);
    QCOMPARE(now->sizeBytes, before.sizeBytes);
    QCOMPARE(now->modified, before.modified);
    // L-SIGNALS, L-MOVED: one of each, the change first and then the move.
    QCOMPARE(told.lines,
        QStringList(
            { s(u"updated ") + QString::number(x.one), s(u"moved ") + from + s(u" > ") + to }));
    // A scan after it finds nothing to do.
    const ScanReport again = x.scan(partSize);
    QCOMPARE(again.unchanged, 5);
    QVERIFY(again.followed.isEmpty() && again.added.isEmpty() && again.nowMissing.isEmpty());
}

void LibraryFollowedTest::aTrackMarkedMissingIsFollowed_data()
{
    QTest::addColumn<QString>("scanned");
    QTest::newRow("the whole folder is scanned") << QString();
    QTest::newRow("only the folder the file is in now") << s(u"c");
}

void LibraryFollowedTest::aTrackMarkedMissingIsFollowed()
{
    // The file went away (an earlier scan marked the track missing) and is back under another
    // name. A track marked missing is a row a file may be, wherever its old path lies.
    QFETCH(QString, scanned);
    Scanned x;
    const QString from = x.f.path(u"a/one.mp3");
    const QString aside = x.f.dir.path() + s(u"/aside.mp3");
    QVERIFY(QFile::rename(from, aside));
    QCOMPARE(x.scan().nowMissing, QList<qint64> { x.one });
    QVERIFY(x.library->track(x.one)->missing);
    const QString to = x.f.path(u"c/uno.mp3");
    QVERIFY(QFile::rename(aside, to));
    Told told(x.library.get());
    const ScanReport r = x.library->scan(
        { scanned.isEmpty() ? x.f.root : x.f.root + QLatin1Char('/') + scanned }, options());
    QCOMPARE(r.followed, QList<qint64> { x.one });
    QCOMPARE(ids(r.found), QString()); // followed, and not also found
    QCOMPARE(ids(r.added), QString());
    QCOMPARE(ids(r.nowMissing), QString());
    const auto now = x.library->track(x.one);
    QVERIFY(now.has_value());
    QCOMPARE(now->path, to);
    QVERIFY(!now->missing);
    QCOMPARE(now->playCount, 3);
    QCOMPARE(x.library->count(), 5);
    QCOMPARE(told.lines,
        QStringList(
            { s(u"updated ") + QString::number(x.one), s(u"moved ") + from + s(u" > ") + to }));
}

void LibraryFollowedTest::twoAlikeFilesAndOneRow_data()
{
    QTest::addColumn<int>("partSize");
    QTest::newRow("a scan in one") << 0;
    QTest::newRow("in parts of one file") << 1;
}

void LibraryFollowedTest::twoAlikeFilesAndOneRow()
{
    // The file was renamed, and a copy of it lies beside it: two files alike in size, time and
    // tags, and one row. Which of them is the track cannot be known, so neither is taken. All
    // is as it was before this rule: the row is marked missing, both files are new tracks.
    QFETCH(int, partSize);
    Scanned x;
    QVERIFY(x.f.move(u"a/one.mp3", u"a/uno.mp3"));
    QVERIFY(QFile::copy(x.f.path(u"a/uno.mp3"), x.f.path(u"a/copy.mp3")));
    QVERIFY(setTime(x.f.path(u"a/copy.mp3"), timeOf(x.f.path(u"a/uno.mp3"))));
    x.f.put(u"basic.wv", u"c/zzz.wv"); // and a file that is plainly new
    const ScanReport r = x.scan(partSize);
    QCOMPARE(ids(r.followed), QString());
    QCOMPARE(r.nowMissing, QList<qint64> { x.one });
    // L-FOLLOW, the order: the files that were not held back first, then those that were.
    QCOMPARE(x.pathsOf(r.added),
        QStringList({ x.f.path(u"c/zzz.wv"), x.f.path(u"a/copy.mp3"), x.f.path(u"a/uno.mp3") }));
    const auto row = x.library->track(x.one);
    QVERIFY(row.has_value());
    QCOMPARE(row->path, x.f.path(u"a/one.mp3"));
    QVERIFY(row->missing);
    QCOMPARE(row->playCount, 3);
    QCOMPARE(x.library->count(), 8);
    for (const qint64 id : r.added) {
        QCOMPARE(x.library->track(id)->playCount, 0);
        QCOMPARE(x.library->track(id)->dateAdded, kLater);
        QCOMPARE(x.library->track(id)->category, s(u"Inbox"));
    }
}

void LibraryFollowedTest::twoAlikeRowsAndOneFile()
{
    // Two tracks alike in size, time and tags have both gone, and one such file is new. Which
    // row it is cannot be known: it is a new track, and both rows are marked missing.
    Scanned x;
    x.f.put(u"basic.mp3", u"a/one2.mp3");
    QVERIFY(setTime(x.f.path(u"a/one2.mp3"), timeOf(x.f.path(u"a/one.mp3"))));
    QCOMPARE(x.scan().added.size(), 1);
    const qint64 other = x.id(u"a/one2.mp3");
    QVERIFY(other > 0 && other != x.one);
    QVERIFY(QFile::remove(x.f.path(u"a/one2.mp3")));
    QVERIFY(x.f.move(u"a/one.mp3", u"c/uno.mp3"));
    const ScanReport r = x.scan();
    QCOMPARE(ids(r.followed), QString());
    QCOMPARE(x.pathsOf(r.added), QStringList({ x.f.path(u"c/uno.mp3") }));
    QCOMPARE(r.nowMissing, QList<qint64>({ x.one, other }));
    QCOMPARE(x.library->track(x.one)->path, x.f.path(u"a/one.mp3"));
    QVERIFY(x.library->track(x.one)->missing);
    QCOMPARE(x.library->track(x.one)->playCount, 3);
    QCOMPARE(x.library->count(), 7);
}

void LibraryFollowedTest::theSameSizeAndTimeAndOtherTags()
{
    // A file of the same size and the same time as a track that has gone, and of another
    // title: it is held back and looked at, and it is not that track.
    Scanned x;
    const qint64 two = x.id(u"a/two.flac");
    QVERIFY(two > 0);
    const QString was = x.f.path(u"a/two.flac");
    const QString title = x.library->track(two)->tags.fields.value(s(u"Title")).value(0);
    QVERIFY(title.size() > 3);
    QByteArray bytes = bytesOf(was);
    const QByteArray text = title.toUtf8();
    QByteArray turned = text;
    std::reverse(turned.begin(), turned.end());
    QVERIFY(turned != text);
    QCOMPARE(bytes.count(text), 1); // what this case stands on: the title is in the file once
    bytes.replace(text, turned);
    const QDateTime when = timeOf(was);
    QVERIFY(QFile::remove(was));
    const QString other = x.f.path(u"a/zwei.flac");
    {
        QFile file(other);
        QVERIFY(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size());
    }
    QVERIFY(setTime(other, when));
    const ScanFindings found = x.look();
    QCOMPARE(found.maybeMoved.size(), 1); // by size and time it may be the track
    QCOMPARE(found.maybeMoved.value(0).rows, QList<qint64> { two });
    const ScanReport r = x.library->applyScan(found, options());
    QCOMPARE(ids(r.followed), QString());
    QCOMPARE(x.pathsOf(r.added), QStringList({ other }));
    QCOMPARE(r.nowMissing, QList<qint64> { two });
    QCOMPARE(x.library->track(r.added.value(0))->tags.fields.value(s(u"Title")),
        QStringList({ QString::fromUtf8(turned) }));
    QCOMPARE(x.library->track(two)->path, was);
    QVERIFY(x.library->track(two)->missing);
}

void LibraryFollowedTest::whatIsHeldBack()
{
    // L-FOLLOW, the looking. A new file is held back only when a row that is missing or gone
    // has its size and its time; every other new file is `fresh`, as before.
    {
        Scanned x; // moved: held back, with the row it may be, and what was known of that row
        QVERIFY(x.f.move(u"a/one.mp3", u"b/uno.mp3"));
        const ScanFindings found = x.look();
        QCOMPARE(found.maybeMoved.size(), 1);
        QCOMPARE(found.maybeMoved.value(0).reading.path, x.f.path(u"b/uno.mp3"));
        QCOMPARE(found.maybeMoved.value(0).rows, QList<qint64> { x.one });
        QCOMPARE(found.fresh.size(), 0);
        QCOMPARE(found.nowMissing, QList<qint64> { x.one });
        QVERIFY(found.judged.contains(x.one));
        QCOMPARE(found.judged.value(x.one).path, x.f.path(u"a/one.mp3"));
    }
    {
        Scanned x; // a track has gone, and a new file of another size has come
        QVERIFY(QFile::remove(x.f.path(u"b/four.ogg")));
        x.f.put(u"basic.wv", u"b/new.wv");
        const ScanFindings found = x.look();
        QCOMPARE(found.maybeMoved.size(), 0);
        QCOMPARE(found.fresh.size(), 1);
    }
    {
        Scanned x; // the same bytes as the track that has gone, written at another time
        const QDateTime when = timeOf(x.f.path(u"a/one.mp3"));
        QVERIFY(QFile::remove(x.f.path(u"a/one.mp3")));
        x.f.put(u"basic.mp3", u"b/uno.mp3");
        QVERIFY(setTime(x.f.path(u"b/uno.mp3"), when.addSecs(-100)));
        const ScanFindings found = x.look();
        QCOMPARE(found.maybeMoved.size(), 0);
        QCOMPARE(found.fresh.size(), 1);
    }
    {
        Scanned x; // a copy of a track that is where it was: no row is missing or gone
        QVERIFY(QFile::copy(x.f.path(u"a/one.mp3"), x.f.path(u"c/again.mp3")));
        QVERIFY(setTime(x.f.path(u"c/again.mp3"), timeOf(x.f.path(u"a/one.mp3"))));
        const ScanFindings found = x.look();
        QCOMPARE(found.maybeMoved.size(), 0);
        QCOMPARE(found.fresh.size(), 1);
        QCOMPARE(found.nowMissing.size(), 0);
    }
}

void LibraryFollowedTest::theRowIsGoneByTheStoring()
{
    // L-PARTS: between the looking and the storing the user removes the track. There is no row
    // to take over; the file is a new track.
    Scanned x;
    QVERIFY(x.f.move(u"a/one.mp3", u"b/uno.mp3"));
    const ScanFindings found = x.look();
    QCOMPARE(x.library->remove({ x.one }), 1);
    const ScanReport r = x.library->applyScan(found, options());
    QCOMPARE(ids(r.followed), QString());
    QCOMPARE(x.pathsOf(r.added), QStringList({ x.f.path(u"b/uno.mp3") }));
    QCOMPARE(ids(r.nowMissing), QString());
    QCOMPARE(r.passedOver, 1);
    QCOMPARE(x.library->count(), 5);
}

void LibraryFollowedTest::thePathIsHeldByTheStoring()
{
    // Between the looking and the storing the file is added by hand. The library holds its
    // path by now: the file is left alone, and the row is marked missing as before.
    Scanned x;
    QVERIFY(x.f.move(u"a/one.mp3", u"b/uno.mp3"));
    const ScanFindings found = x.look();
    const qint64 byHand = x.library->addFile(x.f.path(u"b/uno.mp3"), s(u"Music"), kNow);
    QVERIFY(byHand > 0 && byHand != x.one);
    const ScanReport r = x.library->applyScan(found, options());
    QVERIFY2(r.refused.isEmpty(), qPrintable(r.refused));
    QCOMPARE(ids(r.followed), QString());
    QCOMPARE(ids(r.added), QString());
    QCOMPARE(r.nowMissing, QList<qint64> { x.one });
    QCOMPARE(x.library->track(x.one)->path, x.f.path(u"a/one.mp3"));
    QCOMPARE(x.library->track(byHand)->path, x.f.path(u"b/uno.mp3"));
    QCOMPARE(x.library->count(), 6);
}

void LibraryFollowedTest::theRowIsElsewhereByTheStoring()
{
    // Between the looking and the storing the user tells the library where the track is: at
    // another file with the same bytes. The row is no longer as it was looked at, so the scan
    // does not move it again. Its finding is passed over, and the file is a new track.
    Scanned x;
    QVERIFY(x.f.move(u"a/one.mp3", u"b/uno.mp3"));
    const ScanFindings found = x.look();
    const QString kopie = x.f.path(u"c/kopie.mp3");
    QVERIFY(QFile::copy(x.f.path(u"b/uno.mp3"), kopie));
    QVERIFY(x.library->setPath(x.one, kopie));
    const ScanReport r = x.library->applyScan(found, options());
    QCOMPARE(ids(r.followed), QString());
    QCOMPARE(x.pathsOf(r.added), QStringList({ x.f.path(u"b/uno.mp3") }));
    QCOMPARE(ids(r.nowMissing), QString());
    QCOMPARE(r.passedOver, 1);
    QCOMPARE(x.library->track(x.one)->path, kopie);
    QVERIFY(!x.library->track(x.one)->missing);
    QCOMPARE(x.library->track(x.one)->playCount, 3);
}

void LibraryFollowedTest::aCancelledScanStoresNothingForIt()
{
    // A scan that is stopped does not know what has gone, so it cannot say what a held-back
    // file is. Nothing is stored for the file: not a new track either, which a later scan
    // could no longer mend. The next scan sees the file again and follows it.
    Scanned x;
    QVERIFY(x.f.move(u"a/one.mp3", u"b/uno.mp3"));
    // The files in order: a/two.flac b/four.ogg b/notaudio.mp3 b/three.m4a b/uno.mp3
    // c/five.opus. The scan is stopped before the last, after the file was looked at.
    ScanOptions stopped = options();
    stopped.progress = [](int done, int) { return done < 5; };
    const ScanReport r = x.library->scan({ x.f.root }, stopped);
    QVERIFY(r.cancelled);
    QCOMPARE(ids(r.followed), QString());
    QCOMPARE(ids(r.added), QString());
    QCOMPARE(ids(r.nowMissing), QString());
    QVERIFY(!x.library->trackByPath(x.f.path(u"b/uno.mp3")));
    QCOMPARE(x.library->track(x.one)->path, x.f.path(u"a/one.mp3"));
    QVERIFY(!x.library->track(x.one)->missing);
    QCOMPARE(x.library->count(), 5);
    ScanFindOptions find;
    find.progress = stopped.progress;
    QCOMPARE(findForScan({ x.f.root }, x.library->scanKnown(), find).maybeMoved.size(), 0);
    QCOMPARE(x.scan().followed, QList<qint64> { x.one });
}

void LibraryFollowedTest::theOrderOfTheSignal()
{
    // L-SIGNALS: one tracksUpdated for the scan: found, then updated, then followed, then gone
    // missing.
    Scanned x;
    const qint64 two = x.id(u"a/two.flac");
    const qint64 four = x.id(u"b/four.ogg");
    const qint64 five = x.id(u"c/five.opus");
    const QString aside = x.f.dir.path() + s(u"/aside.opus");
    QVERIFY(QFile::rename(x.f.path(u"c/five.opus"), aside));
    QCOMPARE(x.scan().nowMissing, QList<qint64> { five });
    QVERIFY(QFile::rename(aside, x.f.path(u"c/five.opus"))); // found
    x.f.put(u"empty.flac", u"a/two.flac"); // updated
    QVERIFY(x.f.move(u"a/one.mp3", u"b/uno.mp3")); // followed
    QVERIFY(QFile::remove(x.f.path(u"b/four.ogg"))); // gone missing
    Told told(x.library.get());
    const ScanReport r = x.scan();
    QCOMPARE(r.found, QList<qint64> { five });
    QCOMPARE(r.updated, QList<qint64> { two });
    QCOMPARE(r.followed, QList<qint64> { x.one });
    QCOMPARE(r.nowMissing, QList<qint64> { four });
    QCOMPARE(told.lines.value(0), s(u"updated ") + ids({ five, two, x.one, four }));
    QCOMPARE(told.lines.size(), 2);
    QVERIFY2(told.lines.value(1).startsWith(s(u"moved ")), qPrintable(told.lines.value(1)));
}

void LibraryFollowedTest::addedUpAddsThem()
{
    ScanReport a;
    a.followed = { 4 };
    ScanReport b;
    b.followed = { 9, 2 };
    QCOMPARE(addedUp(a, b).followed, QList<qint64>({ 4, 9, 2 }));
}

QTEST_GUILESS_MAIN(LibraryFollowedTest)
#include "tst_library_followed.moc"
