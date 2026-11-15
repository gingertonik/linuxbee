// LOCKED ACCEPTANCE TEST (a scan and a library that changed meanwhile). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Every expected value is a rule in docs/spec/library.md (L-PARTS), applied
// to copies of the fixtures in tests/data/tags/. Between the looking and
// the storing the library is changed, as a user can while a scan runs.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

#include <atomic>
#include <thread>

#include "library/library.h"

using namespace linuxbee;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

const QDateTime kNow = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);

// A music folder in a temporary place:
//   a/one.mp3  a/two.flac  b/three.m4a  b/four.ogg  b/notaudio.mp3  c/five.opus
struct Folder {
    QTemporaryDir dir;
    QString root;
    Folder()
    {
        root = QDir::cleanPath(QFileInfo(dir.path()).canonicalFilePath()) + QStringLiteral("/music");
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
        if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/") + s(fixtureName), path(relative)))
            QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
    }
    void drop(const char16_t *relative) { QFile::remove(path(relative)); }
};

ScanOptions options(const QString &category = QStringLiteral("Music"))
{
    ScanOptions o;
    o.category = category;
    o.now = kNow;
    return o;
}

QString title(const Library &library, qint64 id)
{
    const auto t = library.track(id);
    return t ? t->tags.fields.value(QStringLiteral("Title")).join(QLatin1Char('|')) : QStringLiteral("<gone>");
}

// One library that has scanned the folder, and the ids of its tracks by file.
struct Scanned {
    Folder f;
    std::unique_ptr<Library> library = Library::open(QStringLiteral(":memory:"));
    Scanned()
    {
        if (library->scan({ f.root }, options()).added.size() != 5)
            QTest::qFail("the fixtures did not scan", __FILE__, __LINE__);
    }
    qint64 id(const char16_t *relative) const
    {
        const auto t = library->trackByPath(f.path(relative));
        return t ? t->id : 0;
    }
    ScanFindings look(const char16_t *folder = u"") const
    {
        return findForScan({ s(folder).isEmpty() ? f.root : f.path(folder) }, library->scanKnown(), { });
    }
};

}

class ScanStaleTest : public QObject {
    Q_OBJECT
private slots:
    void whatWasKnownIsCarried();
    void movedMeanwhile();
    void readAgainMeanwhile();
    void foundMeanwhile();
    void removedMeanwhile();
    void addedUpCountsThem();
};

void ScanStaleTest::whatWasKnownIsCarried()
{
    // L-PARTS: each finding about a known track carries what the library was known to hold.
    Scanned x;
    const qint64 two = x.id(u"a/two.flac");
    const qint64 four = x.id(u"b/four.ogg");
    QVERIFY(two > 0 && four > 0);
    const ScanKnown known = x.library->scanKnown();
    QCOMPARE(x.look().judged.size(), 0); // nothing differs: nothing is judged

    x.f.put(u"empty.flac", u"a/two.flac"); // differs
    x.f.drop(u"b/four.ogg"); // gone
    const ScanFindings found = x.look();
    QCOMPARE(found.reread.size(), 1);
    QCOMPARE(found.nowMissing, QList<qint64> { four });
    QCOMPARE(found.judged.size(), 2);
    QVERIFY(found.judged.contains(two) && found.judged.contains(four));
    const ScanKnown::Entry was = known.byPath.value(x.f.path(u"a/two.flac"));
    QCOMPARE(found.judged.value(two).path, x.f.path(u"a/two.flac"));
    QCOMPARE(found.judged.value(two).sizeBytes, was.sizeBytes);
    QCOMPARE(found.judged.value(two).modified, was.modified);
    QCOMPARE(found.judged.value(two).missing, false);
    QCOMPARE(found.judged.value(four).path, x.f.path(u"b/four.ogg"));

    // Nothing changed in the library meanwhile: everything is applied, nothing passed over.
    const ScanReport report = x.library->applyScan(found, options());
    QCOMPARE(report.updated, QList<qint64> { two });
    QCOMPARE(report.nowMissing, QList<qint64> { four });
    QCOMPARE(report.passedOver, 0);

    // In parts, each part carries what its own findings need, and the answer the rest.
    x.f.put(u"basic.flac", u"a/two.flac");
    x.f.put(u"basic.ogg", u"b/four.ogg"); // four is marked missing and is there again: found
    x.f.drop(u"c/five.opus");
    QList<ScanFindings> parts;
    ScanFindOptions inParts;
    inParts.partSize = 1;
    inParts.part = [&parts](const ScanFindings &part) { parts.append(part); };
    const ScanFindings last = findForScan({ x.f.root }, x.library->scanKnown(), inParts);
    int judged = 0;
    for (const ScanFindings &part : std::as_const(parts)) {
        for (auto it = part.judged.cbegin(); it != part.judged.cend(); ++it) {
            bool named = part.found.contains(it.key());
            for (const ScanFindings::Reread &again : part.reread)
                named = named || again.id == it.key();
            QVERIFY2(named, "a part carries a track none of its findings names");
        }
        judged += int(part.judged.size());
    }
    QCOMPARE(judged, 2); // two.flac read again, four.ogg found
    QCOMPARE(last.nowMissing.size(), 1);
    QCOMPARE(last.judged.size(), 1);
    QVERIFY(last.judged.contains(last.nowMissing.first()));
}

void ScanStaleTest::movedMeanwhile()
{
    // L-PARTS: a track moved by setPath after it was looked at is not marked missing.
    Scanned x;
    const qint64 one = x.id(u"a/one.mp3");
    const QString moved = x.f.path(u"c/one.mp3");
    QVERIFY(QFile::rename(x.f.path(u"a/one.mp3"), moved)); // the organiser moves the file
    const ScanFindings found = x.look(u"a"); // the scan sees it gone
    QCOMPARE(found.nowMissing, QList<qint64> { one });
    QVERIFY(x.library->setPath(one, moved)); // and then tells the library
    QSignalSpy updated(x.library.get(), &Library::tracksUpdated);
    const ScanReport report = x.library->applyScan(found, options());
    QCOMPARE(report.nowMissing, QList<qint64> { });
    QCOMPARE(report.passedOver, 1);
    QCOMPARE(updated.count(), 0);
    const auto now = x.library->track(one);
    QVERIFY(now.has_value());
    QVERIFY(!now->missing);
    QCOMPARE(now->path, moved);
}

void ScanStaleTest::readAgainMeanwhile()
{
    // L-PARTS: an older reading does not replace what the library read later.
    Scanned x;
    const qint64 two = x.id(u"a/two.flac");
    const QString before = title(*x.library, two);
    QVERIFY(!before.isEmpty());
    x.f.put(u"empty.flac", u"a/two.flac"); // changed outside
    const ScanFindings found = x.look(); // the scan reads it: no title
    QCOMPARE(found.reread.size(), 1);
    QVERIFY(found.reread.first().reading.tags.fields.value(QStringLiteral("Title")).isEmpty());
    // Meanwhile the file is written once more and the library reads it (an edit of its tags).
    x.f.put(u"basic.flac", u"a/two.flac");
    QFile file(x.f.path(u"a/two.flac"));
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.setFileTime(kNow.addDays(1), QFileDevice::FileModificationTime));
    file.close();
    QVERIFY(x.library->refresh(two));
    QCOMPARE(title(*x.library, two), before);

    QSignalSpy updated(x.library.get(), &Library::tracksUpdated);
    const ScanReport report = x.library->applyScan(found, options());
    QCOMPARE(report.updated, QList<qint64> { });
    QCOMPARE(report.passedOver, 1);
    QCOMPARE(updated.count(), 0);
    QCOMPARE(title(*x.library, two), before); // not the older, empty reading

    // The next scan sees the file as the library holds it.
    const ScanReport next = x.library->scan({ x.f.root }, options());
    QCOMPARE(next.updated.size(), 0);
    QCOMPARE(next.unchanged, 5);
    QCOMPARE(next.passedOver, 0);
}

void ScanStaleTest::foundMeanwhile()
{
    // L-PARTS: a finding about a track that another scan dealt with meanwhile is passed over.
    Scanned x;
    const qint64 five = x.id(u"c/five.opus");
    x.f.drop(u"c/five.opus");
    QCOMPARE(x.library->scan({ x.f.root }, options()).nowMissing, QList<qint64> { five });
    x.f.put(u"basic.opus", u"c/five.opus");
    {
        // The file is back exactly as stored, so only the missing mark will have changed.
        QFile file(x.f.path(u"c/five.opus"));
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(x.library->track(five)->modified, QFileDevice::FileModificationTime));
    }
    const ScanFindings found = x.look();
    QCOMPARE(found.found, QList<qint64> { five });
    QCOMPARE(found.reread.size(), 0);
    QCOMPARE(found.judged.value(five).missing, true);
    QCOMPARE(x.library->scan({ x.f.root }, options()).found, QList<qint64> { five }); // another scan first
    QSignalSpy updated(x.library.get(), &Library::tracksUpdated);
    const ScanReport report = x.library->applyScan(found, options());
    QCOMPARE(report.found, QList<qint64> { });
    QCOMPARE(report.updated, QList<qint64> { });
    QCOMPARE(report.passedOver, 1);
    QCOMPARE(updated.count(), 0);
}

void ScanStaleTest::removedMeanwhile()
{
    // L-PARTS: a finding about a track the library no longer holds is passed over.
    Scanned x;
    const qint64 two = x.id(u"a/two.flac");
    x.f.put(u"empty.flac", u"a/two.flac");
    const ScanFindings found = x.look();
    QCOMPARE(found.reread.size(), 1);
    QCOMPARE(x.library->remove({ two }), 1);
    const ScanReport report = x.library->applyScan(found, options());
    QCOMPARE(report.updated, QList<qint64> { });
    QCOMPARE(report.added, QList<qint64> { });
    QCOMPARE(report.passedOver, 1);
    QCOMPARE(x.library->count(), 4);
}

void ScanStaleTest::addedUpCountsThem()
{
    ScanReport a;
    a.passedOver = 1;
    ScanReport b;
    b.passedOver = 2;
    QCOMPARE(addedUp(a, b).passedOver, 3);
}

QTEST_GUILESS_MAIN(ScanStaleTest)
#include "tst_library_scanstale.moc"
