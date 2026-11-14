// LOCKED ACCEPTANCE TEST (a scan in two steps). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Every expected value is a rule in docs/spec/library.md (L-PARTS), applied
// to copies of the fixtures in tests/data/tags/. Two libraries look at one
// folder: one by scan(), one by the two steps; they must end up alike.
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

QString told(const ScanReport &r)
{
    const auto ids = [](const QList<qint64> &list) {
        QStringList out;
        for (const qint64 id : list)
            out.append(QString::number(id));
        return out.join(QLatin1Char(','));
    };
    return QStringLiteral("added[%1] updated[%2] nowMissing[%3] found[%4] unchanged=%5 notAudio[%6] names=%7 in[%8] cancelled=%9")
        .arg(ids(r.added), ids(r.updated), ids(r.nowMissing), ids(r.found))
        .arg(r.unchanged)
        .arg(r.notAudio.join(QLatin1Char('|')))
        .arg(r.unreadableNames)
        .arg(r.unreadableIn.join(QLatin1Char('|')))
        .arg(r.cancelled ? 1 : 0);
}

// Everything the library holds, as text, so that two libraries can be compared.
QStringList held(const Library &library)
{
    QStringList out;
    for (const LibraryTrack &t : library.tracks()) {
        QStringList fields;
        for (auto it = t.tags.fields.cbegin(); it != t.tags.fields.cend(); ++it)
            fields.append(it.key() + QLatin1Char('=') + it.value().join(QLatin1Char(';')));
        fields.sort();
        out.append(QStringLiteral("%1 %2 %3 size=%4 modified=%5 added=%6 missing=%7 pictures=%8 plays=%9 ")
                       .arg(t.id)
                       .arg(t.path, t.category)
                       .arg(t.sizeBytes)
                       .arg(t.modified.toSecsSinceEpoch())
                       .arg(t.dateAdded.toSecsSinceEpoch())
                       .arg(t.missing ? 1 : 0)
                       .arg(t.pictureCount)
                       .arg(t.playCount)
            + t.tags.format + QLatin1Char(' ') + fields.join(QLatin1Char('|')));
    }
    return out;
}

// One library scanned in one step, the other in two, over the same folder.
struct Pair {
    std::unique_ptr<Library> one = Library::open(QStringLiteral(":memory:"));
    std::unique_ptr<Library> two = Library::open(QStringLiteral(":memory:"));

    // Scans both and says what differed; empty: nothing did.
    QString both(const QStringList &folders, const ScanOptions &o = options())
    {
        QSignalSpy addedOne(one.get(), &Library::tracksAdded);
        QSignalSpy updatedOne(one.get(), &Library::tracksUpdated);
        QSignalSpy addedTwo(two.get(), &Library::tracksAdded);
        QSignalSpy updatedTwo(two.get(), &Library::tracksUpdated);
        const ScanReport a = one->scan(folders, o);
        ScanFindOptions find;
        find.progress = o.progress;
        const ScanReport b = two->applyScan(findForScan(folders, two->scanKnown(), find), o);
        if (told(a) != told(b))
            return QStringLiteral("reports: ") + told(a) + QStringLiteral(" / ") + told(b);
        if (held(*one) != held(*two))
            return QStringLiteral("libraries differ after ") + told(a);
        if (addedOne.count() != addedTwo.count() || updatedOne.count() != updatedTwo.count())
            return QStringLiteral("signals differ after ") + told(a);
        if (addedTwo.count() > 1 || updatedTwo.count() > 1)
            return QStringLiteral("more than one signal of a kind");
        // L-PARTS: tracksUpdated names what was found, then updated, then gone missing.
        for (const QSignalSpy *spy : { &updatedOne, &updatedTwo }) {
            if (spy->count() == 1 && spy->at(0).at(0).value<QList<qint64>>() != a.found + a.updated + a.nowMissing)
                return QStringLiteral("tracksUpdated does not name found, updated, nowMissing in that order after ") + told(a);
        }
        if (addedTwo.count() == 1 && addedTwo.at(0).at(0).value<QList<qint64>>() != a.added)
            return QStringLiteral("tracksAdded does not name what was added after ") + told(a);
        last = a;
        return QString();
    }
    ScanReport last;
};

}

class ScanPartsTest : public QObject {
    Q_OBJECT
private slots:
    void whatTheLibraryKnows();
    void twoStepsAreTheScan();
    void cancelled();
    void inParts();
    void offTheLibrarysThread();
    void nothingToDo();
};

void ScanPartsTest::whatTheLibraryKnows()
{
    // L-PARTS: scanKnown() is every track by its path, and reads no track.
    Folder f;
    auto library = Library::open(QStringLiteral(":memory:"));
    QCOMPARE(library->scanKnown().byPath.size(), 0);
    QCOMPARE(library->scan({ f.root }, options()).added.size(), 5);
    f.drop(u"a/two.flac");
    QCOMPARE(library->scan({ f.root }, options()).nowMissing.size(), 1);

    const qint64 read = library->tracksRead();
    const ScanKnown known = library->scanKnown();
    QCOMPARE(library->tracksRead(), read);
    QCOMPARE(known.byPath.size(), 5);
    int missing = 0;
    for (const LibraryTrack &t : library->tracks()) {
        QVERIFY2(known.byPath.contains(t.path), qPrintable(t.path));
        const ScanKnown::Entry e = known.byPath.value(t.path);
        QCOMPARE(e.id, t.id);
        QCOMPARE(e.sizeBytes, t.sizeBytes);
        QCOMPARE(e.modified, t.modified);
        QCOMPARE(e.missing, t.missing);
        missing += e.missing ? 1 : 0;
    }
    QCOMPARE(missing, 1);
}

void ScanPartsTest::twoStepsAreTheScan()
{
    // L-PARTS: applyScan(findForScan(folders, scanKnown())) is scan(folders).
    Folder f;
    Pair p;
    QCOMPARE(p.both({ f.root }), QString());
    QCOMPARE(p.last.added.size(), 5);
    QCOMPARE(p.last.notAudio.size(), 1);

    QCOMPARE(p.both({ f.root }), QString()); // nothing changed
    QCOMPARE(p.last.unchanged, 5);

    f.put(u"basic.wav", u"a/one.mp3"); // a known file that differs (and is not what its name says)
    f.put(u"v23.mp3", u"b/new.mp3"); // a new one
    f.drop(u"b/three.m4a"); // one that has gone
    QCOMPARE(p.both({ f.root }), QString());
    QCOMPARE(p.last.added.size(), 1);
    QCOMPARE(p.last.nowMissing.size(), 1);

    f.put(u"basic.m4a", u"b/three.m4a"); // back, as it was
    f.drop(u"c/five.opus");
    QCOMPARE(p.both({ f.root }), QString());
    QCOMPARE(p.last.found.size(), 1);
    QCOMPARE(p.last.nowMissing.size(), 1);

    // Two gone at once: they are named in ascending order of path.
    f.drop(u"b/four.ogg");
    f.drop(u"a/two.flac");
    QCOMPARE(p.both({ f.root }), QString());
    QCOMPARE(p.last.nowMissing.size(), 2);
    {
        const auto first = p.one->track(p.last.nowMissing.at(0));
        const auto second = p.one->track(p.last.nowMissing.at(1));
        QVERIFY(first && second);
        QVERIFY2(first->path < second->path, qPrintable(first->path + s(u" / ") + second->path));
    }
    f.put(u"basic.ogg", u"b/four.ogg");
    f.put(u"basic.flac", u"a/two.flac");
    QCOMPARE(p.both({ f.root }), QString());
    QCOMPARE(p.last.found.size(), 2);

    f.put(u"basic.flac", u"c/five.opus"); // back, and different: found, read again, not "updated"
    QCOMPARE(p.both({ f.root }), QString());
    QCOMPARE(p.last.found.size(), 1);
    QCOMPARE(p.last.updated.size(), 0);

    // One folder of several; a folder inside another; one that is not there; another section.
    f.put(u"basic.ogg", u"a/later.ogg");
    QCOMPARE(p.both({ f.root + s(u"/a"), f.root + s(u"/a"), f.root + s(u"/nowhere") }, options(s(u"Inbox"))), QString());
    QCOMPARE(p.last.added.size(), 1);
    f.drop(u"b/four.ogg");
    QCOMPARE(p.both({ f.root + s(u"/c") }), QString()); // b is not looked at: nothing goes missing
    QCOMPARE(p.last.nowMissing.size(), 0);
    QCOMPARE(p.both({ f.root, f.root + s(u"/b") }), QString());
    QCOMPARE(p.last.nowMissing.size(), 1);
}

void ScanPartsTest::cancelled()
{
    // L-PARTS: a find that was stopped stores what it had found, and marks nothing missing.
    Folder f;
    Pair p;
    QCOMPARE(p.both({ f.root }), QString());
    f.drop(u"a/one.mp3");
    f.put(u"v23.mp3", u"a/aaa.mp3");
    f.put(u"v23.mp3", u"c/zzz.mp3");
    ScanOptions o = options();
    o.progress = [](int done, int) { return done < 2; };
    QCOMPARE(p.both({ f.root }, o), QString());
    QVERIFY(p.last.cancelled);
    QCOMPARE(p.last.added.size(), 1); // a/aaa.mp3 came before the stop; c/zzz.mp3 did not
    QCOMPARE(p.last.nowMissing.size(), 0);
}

void ScanPartsTest::inParts()
{
    // L-PARTS: the same findings handed over two files at a time.
    Folder f;
    Pair p;
    QCOMPARE(p.both({ f.root + s(u"/a") }), QString()); // two known already
    f.drop(u"a/two.flac");

    QList<ScanFindings> parts;
    ScanFindOptions find;
    find.partSize = 2;
    find.part = [&parts](const ScanFindings &part) { parts.append(part); };
    const ScanFindings rest = findForScan({ f.root }, p.two->scanKnown(), find);
    // Five music files are there now: one.mp3, three.m4a, four.ogg, notaudio.mp3, five.opus.
    QCOMPARE(parts.size(), 2); // after the second and the fourth file
    for (const ScanFindings &part : std::as_const(parts)) {
        QCOMPARE(part.fresh.size() + part.reread.size() + part.found.size() + part.unchanged + part.notAudio.size(), 2);
        QCOMPARE(part.nowMissing.size(), 0); // only the last piece says what has gone
        QVERIFY(!part.cancelled);
    }
    QCOMPARE(rest.fresh.size() + rest.unchanged + rest.notAudio.size(), 1);
    QCOMPARE(rest.nowMissing.size(), 1);

    const ScanReport whole = p.one->scan({ f.root }, options());
    ScanReport sum;
    for (const ScanFindings &part : std::as_const(parts))
        sum = addedUp(sum, p.two->applyScan(part, options()));
    sum = addedUp(sum, p.two->applyScan(rest, options()));
    QCOMPARE(told(sum), told(whole));
    QCOMPARE(held(*p.two), held(*p.one));

    // Without a part function, or with a size of 0, it all comes back at once.
    f.put(u"v23.mp3", u"c/more.mp3");
    ScanFindOptions noFunction;
    noFunction.partSize = 2;
    QCOMPARE(findForScan({ f.root }, p.two->scanKnown(), noFunction).unchanged, 4);
    int calls = 0;
    ScanFindOptions noSize;
    noSize.part = [&calls](const ScanFindings &) { ++calls; };
    QCOMPARE(findForScan({ f.root }, p.two->scanKnown(), noSize).fresh.size(), 1);
    QCOMPARE(calls, 0);
}

void ScanPartsTest::offTheLibrarysThread()
{
    // L-PARTS: the find touches no library, so it can run on another thread.
    Folder f;
    Pair p;
    const ScanKnown known = p.two->scanKnown();
    const QStringList folders { f.root };
    ScanFindings found;
    std::atomic<int> calls { 0 };
    std::atomic<bool> onAnother { true };
    const std::thread::id here = std::this_thread::get_id();
    std::thread worker([&] {
        ScanFindOptions find;
        find.progress = [&](int, int) {
            ++calls;
            if (std::this_thread::get_id() == here)
                onAnother = false;
            return true;
        };
        found = findForScan(folders, known, find);
    });
    worker.join();
    QCOMPARE(calls.load(), 6); // before each of the six files
    QVERIFY(onAnother.load());
    const ScanReport a = p.one->scan(folders, options());
    const ScanReport b = p.two->applyScan(found, options());
    QCOMPARE(told(b), told(a));
    QCOMPARE(held(*p.two), held(*p.one));
}

void ScanPartsTest::nothingToDo()
{
    // L-PARTS: applyScan refuses what scan refuses, and empty findings change nothing.
    Folder f;
    auto library = Library::open(QStringLiteral(":memory:"));
    const ScanFindings found = findForScan({ f.root }, library->scanKnown());
    QCOMPARE(found.fresh.size(), 5);
    QSignalSpy added(library.get(), &Library::tracksAdded);
    ScanOptions bad = options();
    bad.now = QDateTime();
    QCOMPARE(told(library->applyScan(found, bad)), told(ScanReport { }));
    QCOMPARE(told(library->applyScan(found, options(s(u"No Such Section")))), told(ScanReport { }));
    QCOMPARE(library->count(), 0);
    QCOMPARE(told(library->applyScan(ScanFindings { }, options())), told(ScanReport { }));
    QCOMPARE(added.count(), 0);
    QCOMPARE(library->applyScan(found, options()).added.size(), 5);
    QCOMPARE(added.count(), 1);
    // L-SCAN: a scan that is refused does not look at a file either.
    int asked = 0;
    bad.progress = [&asked](int, int) {
        ++asked;
        return true;
    };
    QCOMPARE(told(library->scan({ f.root }, bad)), told(ScanReport { }));
    ScanOptions noSection = options(s(u"No Such Section"));
    noSection.progress = bad.progress;
    QCOMPARE(told(library->scan({ f.root }, noSection)), told(ScanReport { }));
    QCOMPARE(asked, 0);
    // The same findings a second time: the files are known by now, nothing is added twice.
    QCOMPARE(library->applyScan(found, options()).added.size(), 0);
    QCOMPARE(library->count(), 5);
}

QTEST_MAIN(ScanPartsTest)
#include "tst_library_scanparts.moc"
