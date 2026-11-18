// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Every expected value is a rule in docs/spec/library.md (L-x), applied to
// copies of the fixtures in tests/data/tags/ (what those files hold is in
// MANIFEST.txt there, written by a different library than the one under test).
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include "library/library.h"

using linuxbee::Library;
using linuxbee::LibraryTrack;
using linuxbee::ScanReport;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

QDateTime utc(int y, int mo, int d, int h = 12, int mi = 0, int sec = 0)
{
    return QDateTime(QDate(y, mo, d), QTime(h, mi, sec), QTimeZone::UTC);
}

const QDateTime kNow = utc(2026, 11, 13);

QString fixture(const QString &name)
{
    return QStringLiteral(LINUXBEE_TEST_DATA "/tags/") + name;
}

// A music folder in a temporary place:
//   a/basic.mp3  a/basic.flac  b/basic.m4a  b/notaudio.mp3  b/notes.txt
struct Folder {
    QTemporaryDir dir;
    QString root;
    Folder()
    {
        root = QDir::cleanPath(QFileInfo(dir.path()).canonicalFilePath()) + QStringLiteral("/music");
        QDir().mkpath(root + QStringLiteral("/a"));
        QDir().mkpath(root + QStringLiteral("/b"));
        put(s(u"basic.mp3"), s(u"a/basic.mp3"));
        put(s(u"basic.flac"), s(u"a/basic.flac"));
        put(s(u"basic.m4a"), s(u"b/basic.m4a"));
        put(s(u"notaudio.mp3"), s(u"b/notaudio.mp3"));
        QFile notes(root + QStringLiteral("/b/notes.txt"));
        if (notes.open(QIODevice::WriteOnly))
            notes.write("x");
    }
    void put(const QString &fixtureName, const QString &relative)
    {
        QFile::remove(path(relative));
        QFile::copy(fixture(fixtureName), path(relative));
        // A known, old modification time, so that "the file changed" is the test's doing.
        QFile f(path(relative));
        if (f.open(QIODevice::ReadWrite))
            f.setFileTime(utc(2020, 1, 2, 3, 4, 5), QFileDevice::FileModificationTime);
    }
    QString path(const QString &relative) const { return root + QLatin1Char('/') + relative; }
    QString db() const { return dir.path() + QStringLiteral("/library.db"); }
};

linuxbee::ScanOptions at(const QDateTime &now, const QString &category = QStringLiteral("Music"))
{
    linuxbee::ScanOptions o;
    o.now = now;
    o.category = category;
    return o;
}

QList<qint64> flat(const QSignalSpy &spy)
{
    QList<qint64> ids;
    for (const QList<QVariant> &call : spy)
        ids += call.at(0).value<QList<qint64>>();
    std::sort(ids.begin(), ids.end());
    return ids;
}

QList<qint64> sorted(QList<qint64> ids)
{
    std::sort(ids.begin(), ids.end());
    return ids;
}

}

class LibraryTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void openAndCategories();
    void scan();
    void storedTags();
    void rescan();
    void scanCategoryAndProgress();
    void persistence();
    void addFile();
    void edits();
    void statistics();
    void relink();
    void fields();
    void refusals();
};

void LibraryTest::initTestCase()
{
    QVERIFY(QFileInfo::exists(fixture(s(u"basic.mp3"))));
    qRegisterMetaType<QList<qint64>>();
}

void LibraryTest::openAndCategories()
{
    // L-CATEGORY
    QCOMPARE(Library::categories(),
        QStringList({ s(u"Music"), s(u"Inbox"), s(u"Audiobooks"), s(u"Podcasts"), s(u"Videos") }));
    QCOMPARE(Library::currentSchemaVersion(), 2);
    // L-OPEN
    QString error = s(u"stale");
    auto memory = Library::open(s(u":memory:"), &error);
    QVERIFY(memory);
    QCOMPARE(memory->count(), 0);
    QVERIFY(memory->tracks().isEmpty());
    QVERIFY(!memory->track(1));
    QVERIFY(Library::open(s(u":memory:"))); // no error wanted; two libraries at once
    QTemporaryDir dir;
    const QString file = dir.path() + QStringLiteral("/new/library.db");
    QVERIFY(!Library::open(file, &error)); // L-OPEN: the folder is not created
    QVERIFY(!error.isEmpty());
    QVERIFY(!QFileInfo::exists(dir.path() + QStringLiteral("/new")));
    QVERIFY(Library::open(dir.path() + QStringLiteral("/library.db"), &error));
    QVERIFY(QFileInfo::exists(dir.path() + QStringLiteral("/library.db")));
}

void LibraryTest::scan()
{
    Folder f;
    auto lib = Library::open(s(u":memory:"));
    QVERIFY(lib);
    QSignalSpy added(lib.get(), &Library::tracksAdded);
    QSignalSpy updated(lib.get(), &Library::tracksUpdated);
    const ScanReport r = lib->scan({ f.root }, at(kNow));
    // L-SCAN: three music files; notaudio.mp3 is reported, notes.txt is not looked at.
    QCOMPARE(r.added.size(), 3);
    QCOMPARE(r.notAudio, QStringList({ f.path(s(u"b/notaudio.mp3")) }));
    QCOMPARE(r.unchanged, 0);
    QVERIFY(r.updated.isEmpty() && r.nowMissing.isEmpty() && r.found.isEmpty() && !r.cancelled);
    QCOMPARE(lib->count(), 3);
    QCOMPARE(added.size(), 1); // L-SIGNALS: one signal for the scan
    QCOMPARE(flat(added), sorted(r.added));
    QCOMPARE(updated.size(), 0);

    // tracks() is in ascending order of path; ids are in the order added, which is the same.
    const QList<LibraryTrack> all = lib->tracks();
    QCOMPARE(all.size(), 3);
    QCOMPARE(all.at(0).path, f.path(s(u"a/basic.flac")));
    QCOMPARE(all.at(1).path, f.path(s(u"a/basic.mp3")));
    QCOMPARE(all.at(2).path, f.path(s(u"b/basic.m4a")));
    QCOMPARE(r.added, QList<qint64>({ all.at(0).id, all.at(1).id, all.at(2).id }));
    QVERIFY(all.at(0).id > 0 && all.at(0).id < all.at(1).id && all.at(1).id < all.at(2).id);
    for (const LibraryTrack &t : all) {
        QCOMPARE(t.category, s(u"Music"));
        QCOMPARE(t.dateAdded, kNow);
        QCOMPARE(t.modified, utc(2020, 1, 2, 3, 4, 5));
        QCOMPARE(t.playCount, 0);
        QCOMPARE(t.skipCount, 0);
        QVERIFY(!t.lastPlayed.isValid());
        QVERIFY(!t.missing);
    }
    QCOMPARE(all.at(1).sizeBytes, qint64(6109));
    QCOMPARE(lib->track(all.at(1).id)->path, all.at(1).path);
    QCOMPARE(lib->trackByPath(f.root + s(u"/a/../a/./basic.mp3"))->id, all.at(1).id); // looked up clean
    QVERIFY(!lib->trackByPath(f.path(s(u"b/notaudio.mp3"))));
    QVERIFY(!lib->track(9999));
    // A folder given twice, or inside another, adds nothing twice.
    const ScanReport again = lib->scan({ f.root, f.root + s(u"/a"), f.root }, at(kNow));
    QCOMPARE(again.added.size(), 0);
    QCOMPARE(again.unchanged, 3);
    QCOMPARE(lib->count(), 3);
    // A folder that is not there is an empty folder.
    QCOMPARE(lib->scan({ f.root + s(u"/nowhere") }, at(kNow)).added.size(), 0);
}

void LibraryTest::storedTags()
{
    // L-STORE: what comes back is what readTags gave, without the picture bytes.
    Folder f;
    auto lib = Library::open(s(u":memory:"));
    QVERIFY(lib);
    lib->scan({ f.root }, at(kNow));
    for (const char16_t *name : { u"a/basic.mp3", u"a/basic.flac", u"b/basic.m4a" }) {
        const auto t = lib->trackByPath(f.path(s(name)));
        QVERIFY(t);
        const linuxbee::ReadResult direct = linuxbee::readTags(f.path(s(name)));
        QVERIFY(direct.status == linuxbee::ReadResult::Status::Ok);
        QCOMPARE(t->tags.fields, direct.tags.fields);
        QCOMPARE(t->tags.other, direct.tags.other);
        QCOMPARE(t->tags.unsupported, direct.tags.unsupported);
        QCOMPARE(t->tags.format, direct.tags.format);
        QCOMPARE(t->tags.tagTypes, direct.tags.tagTypes);
        QCOMPARE(t->tags.audio.durationMs, direct.tags.audio.durationMs);
        QCOMPARE(t->tags.audio.bitrateKbps, direct.tags.audio.bitrateKbps);
        QCOMPARE(t->tags.audio.sampleRateHz, direct.tags.audio.sampleRateHz);
        QCOMPARE(t->tags.audio.channels, direct.tags.audio.channels);
        QCOMPARE(t->tags.audio.bitsPerSample, direct.tags.audio.bitsPerSample);
        QCOMPARE(t->pictureCount, int(direct.tags.pictures.size()));
        QVERIFY(t->tags.pictures.isEmpty());
    }
    // Two spot checks against MANIFEST.txt, so that an empty answer on both sides cannot pass.
    const auto flac = lib->trackByPath(f.path(s(u"a/basic.flac")));
    QCOMPARE(flac->tags.fields.value(s(u"Artist")), QStringList({ s(u"First Artist"), s(u"Second Artist") }));
    QCOMPARE(flac->tags.fields.value(s(u"Album")), QStringList({ s(u"Flac Album") }));
    QCOMPARE(flac->pictureCount, 1);
    QCOMPARE(flac->tags.audio.sampleRateHz, 44100);
}

void LibraryTest::rescan()
{
    Folder f;
    auto lib = Library::open(s(u":memory:"));
    QVERIFY(lib);
    lib->scan({ f.root }, at(kNow));
    const qint64 mp3 = lib->trackByPath(f.path(s(u"a/basic.mp3")))->id;
    const qint64 flac = lib->trackByPath(f.path(s(u"a/basic.flac")))->id;
    const qint64 m4a = lib->trackByPath(f.path(s(u"b/basic.m4a")))->id;
    lib->recordPlay(mp3, utc(2026, 11, 14));
    const QDateTime later = utc(2026, 12, 1);

    // L-SCAN: a file whose size or time changed is read again; what only the library knows is kept.
    QFile::remove(f.path(s(u"a/basic.mp3")));
    QFile::copy(fixture(s(u"v23.mp3")), f.path(s(u"a/basic.mp3"))); // other tags, new time
    QFile::remove(f.path(s(u"b/basic.m4a")));
    f.put(s(u"basic.opus"), s(u"b/new.opus"));
    QSignalSpy added(lib.get(), &Library::tracksAdded);
    QSignalSpy updated(lib.get(), &Library::tracksUpdated);
    QSignalSpy removed(lib.get(), &Library::tracksRemoved);
    const ScanReport r = lib->scan({ f.root }, at(later));
    QCOMPARE(r.updated, QList<qint64>({ mp3 }));
    QCOMPARE(r.nowMissing, QList<qint64>({ m4a }));
    QCOMPARE(r.added.size(), 1);
    QCOMPARE(r.unchanged, 1); // the flac
    QVERIFY(r.found.isEmpty());
    QCOMPARE(lib->count(), 4); // L-MISSING: a missing track is kept
    QCOMPARE(removed.size(), 0);
    QCOMPARE(flat(added), r.added);
    QCOMPARE(updated.size(), 1);
    QCOMPARE(flat(updated), sorted({ mp3, m4a }));

    const auto changed = lib->track(mp3);
    QCOMPARE(changed->tags.fields, linuxbee::readTags(fixture(s(u"v23.mp3"))).tags.fields);
    QCOMPARE(changed->sizeBytes, qint64(5949));
    QCOMPARE(changed->dateAdded, kNow); // not the later scan's time
    QCOMPARE(changed->playCount, 1);
    QCOMPARE(changed->lastPlayed, utc(2026, 11, 14));
    QVERIFY(lib->track(m4a)->missing);
    QCOMPARE(lib->track(m4a)->tags.fields.value(s(u"Title")), QStringList({ s(u"M4A Title") })); // still known
    QVERIFY(!lib->track(flac)->missing);
    QCOMPARE(lib->track(r.added.at(0))->dateAdded, later);

    // A scan of another folder says nothing about this one's tracks.
    QFile::remove(f.path(s(u"a/basic.flac")));
    const ScanReport other = lib->scan({ f.root + s(u"/b") }, at(later));
    QVERIFY(other.nowMissing.isEmpty());
    QVERIFY(!lib->track(flac)->missing);
    QCOMPARE(other.unchanged, 1); // new.opus; the missing m4a is not counted

    // L-MISSING: the file comes back.
    f.put(s(u"basic.m4a"), s(u"b/basic.m4a"));
    const ScanReport back = lib->scan({ f.root }, at(later));
    QCOMPARE(back.found, QList<qint64>({ m4a }));
    QCOMPARE(back.nowMissing, QList<qint64>({ flac }));
    QVERIFY(back.added.isEmpty() && back.updated.isEmpty());
    QVERIFY(!lib->track(m4a)->missing);
    QCOMPARE(lib->track(m4a)->dateAdded, kNow);
    // A missing track stays missing, and is not reported twice.
    const ScanReport quiet = lib->scan({ f.root }, at(later));
    QVERIFY(quiet.nowMissing.isEmpty() && quiet.found.isEmpty());
    QCOMPARE(quiet.unchanged, 3);
    // The same size with another modification time is a changed file.
    {
        QFile touched(f.path(s(u"b/basic.m4a")));
        QVERIFY(touched.open(QIODevice::ReadWrite));
        QVERIFY(touched.setFileTime(utc(2021, 6, 7, 8, 9, 10), QFileDevice::FileModificationTime));
    }
    const ScanReport touchedScan = lib->scan({ f.root }, at(later));
    QCOMPARE(touchedScan.updated, QList<qint64>({ m4a }));
    QCOMPARE(touchedScan.unchanged, 2);
    QCOMPARE(lib->track(m4a)->modified, utc(2021, 6, 7, 8, 9, 10));
}

void LibraryTest::scanCategoryAndProgress()
{
    Folder f;
    auto lib = Library::open(s(u":memory:"));
    QVERIFY(lib);
    // L-CATEGORY: new files get the scan's category; known files keep theirs.
    lib->scan({ f.root + s(u"/a") }, at(kNow, s(u"Inbox")));
    lib->scan({ f.root }, at(kNow, s(u"Audiobooks")));
    QCOMPARE(lib->tracks(s(u"Inbox")).size(), 2);
    QCOMPARE(lib->tracks(s(u"Audiobooks")).size(), 1);
    QCOMPARE(lib->tracks(s(u"Audiobooks")).at(0).path, f.path(s(u"b/basic.m4a")));
    QVERIFY(lib->tracks(s(u"Music")).isEmpty());
    QCOMPARE(lib->tracks().size(), 3);

    // L-SCAN: progress is called before each file with (done, total); false stops the scan.
    auto fresh = Library::open(s(u":memory:"));
    QList<QPair<int, int>> calls;
    linuxbee::ScanOptions o = at(kNow);
    o.progress = [&calls](int done, int total) {
        calls.append({ done, total });
        return done < 2;
    };
    const ScanReport r = fresh->scan({ f.root }, o);
    QVERIFY(r.cancelled);
    QCOMPARE(calls, (QList<QPair<int, int>> { { 0, 4 }, { 1, 4 }, { 2, 4 } })); // 4: notaudio.mp3 counts
    QCOMPARE(r.added.size(), 2);
    QCOMPARE(fresh->count(), 2);
    QVERIFY(r.nowMissing.isEmpty());
}

void LibraryTest::persistence()
{
    // L-OPEN: everything survives closing and opening again.
    Folder f;
    qint64 mp3 = 0;
    {
        auto lib = Library::open(f.db());
        QVERIFY(lib);
        lib->scan({ f.root }, at(kNow));
        mp3 = lib->trackByPath(f.path(s(u"a/basic.mp3")))->id;
        lib->recordPlay(mp3, utc(2026, 11, 20, 8, 30, 15));
        lib->setCategory({ mp3 }, s(u"Podcasts"));
    }
    {
        QString error;
        auto lib = Library::open(f.db(), &error);
        QVERIFY2(lib, qPrintable(error));
        QCOMPARE(lib->count(), 3);
        const auto t = lib->track(mp3);
        QVERIFY(t);
        QCOMPARE(t->path, f.path(s(u"a/basic.mp3")));
        QCOMPARE(t->category, s(u"Podcasts"));
        QCOMPARE(t->playCount, 1);
        QCOMPARE(t->lastPlayed, utc(2026, 11, 20, 8, 30, 15));
        QCOMPARE(t->dateAdded, kNow);
        QCOMPARE(t->tags.fields, linuxbee::readTags(f.path(s(u"a/basic.mp3"))).tags.fields);
        QCOMPARE(lib->scan({ f.root }, at(kNow)).unchanged, 3);
    }
    // L-SCHEMA: the file is SQLite and says which schema it has.
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(s(u"QSQLITE"), s(u"judge"));
        db.setDatabaseName(f.db());
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec(s(u"PRAGMA user_version")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 2);
        // A library from a newer linuxBee is refused, not altered.
        QVERIFY(q.exec(s(u"PRAGMA user_version = 99")));
        db.close();
    }
    QSqlDatabase::removeDatabase(s(u"judge"));
    QString error;
    QVERIFY(!Library::open(f.db(), &error));
    QVERIFY(!error.isEmpty());
    // A file that is not a database is refused.
    QFile junk(f.dir.path() + QStringLiteral("/junk.db"));
    QVERIFY(junk.open(QIODevice::WriteOnly));
    junk.write(QByteArray(4096, 'x'));
    junk.close();
    error.clear();
    QVERIFY(!Library::open(junk.fileName(), &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(QFileInfo(junk.fileName()).size(), qint64(4096)); // and left as it was
}

void LibraryTest::addFile()
{
    // L-ADD
    Folder f;
    auto lib = Library::open(s(u":memory:"));
    QVERIFY(lib);
    QSignalSpy added(lib.get(), &Library::tracksAdded);
    const qint64 id = lib->addFile(f.root + s(u"/a/./basic.mp3"), s(u"Inbox"), kNow);
    QVERIFY(id > 0);
    QCOMPARE(lib->track(id)->path, f.path(s(u"a/basic.mp3")));
    QCOMPARE(lib->track(id)->category, s(u"Inbox"));
    QCOMPARE(lib->track(id)->dateAdded, kNow);
    QCOMPARE(flat(added), QList<qint64>({ id }));
    // Again: the same id, nothing changes, no signal.
    QCOMPARE(lib->addFile(f.path(s(u"a/basic.mp3")), s(u"Music"), utc(2027, 1, 1)), id);
    QCOMPARE(lib->track(id)->category, s(u"Inbox"));
    QCOMPARE(lib->track(id)->dateAdded, kNow);
    QCOMPARE(added.size(), 1);
    // Not a music file, not there, not a category: 0.
    QCOMPARE(lib->addFile(f.path(s(u"b/notaudio.mp3")), s(u"Music"), kNow), qint64(0));
    QCOMPARE(lib->addFile(f.path(s(u"b/gone.mp3")), s(u"Music"), kNow), qint64(0));
    QCOMPARE(lib->addFile(f.path(s(u"a/basic.flac")), s(u"Stuff"), kNow), qint64(0));
    QCOMPARE(lib->count(), 1);
    // A file without a music extension is added when it is audio: the caller chose it.
    QFile::copy(fixture(s(u"noextension")), f.path(s(u"a/song")));
    QVERIFY(lib->addFile(f.path(s(u"a/song")), s(u"Music"), kNow) > 0);
}

void LibraryTest::edits()
{
    Folder f;
    auto lib = Library::open(s(u":memory:"));
    QVERIFY(lib);
    lib->scan({ f.root }, at(kNow));
    const qint64 mp3 = lib->trackByPath(f.path(s(u"a/basic.mp3")))->id;
    const qint64 flac = lib->trackByPath(f.path(s(u"a/basic.flac")))->id;
    const qint64 m4a = lib->trackByPath(f.path(s(u"b/basic.m4a")))->id;
    QSignalSpy updated(lib.get(), &Library::tracksUpdated);
    QSignalSpy removed(lib.get(), &Library::tracksRemoved);

    // L-EDIT setCategory: counts the tracks it changed.
    QCOMPARE(lib->setCategory({ mp3, flac, 9999 }, s(u"Audiobooks")), 2);
    QCOMPARE(lib->setCategory({ mp3, m4a }, s(u"Audiobooks")), 1); // mp3 is there already
    QCOMPARE(lib->setCategory({ mp3 }, s(u"Nonsense")), 0);
    QCOMPARE(lib->track(mp3)->category, s(u"Audiobooks"));
    QCOMPARE(updated.size(), 2);
    QCOMPARE(flat(updated), sorted({ mp3, flac, m4a }));
    updated.clear();

    // refresh: the file is read again now, whatever its time says.
    f.put(s(u"v23.mp3"), s(u"a/basic.mp3")); // same old time as before
    QVERIFY(lib->refresh(mp3));
    QCOMPARE(lib->track(mp3)->tags.fields, linuxbee::readTags(fixture(s(u"v23.mp3"))).tags.fields);
    QCOMPARE(lib->track(mp3)->sizeBytes, qint64(5949));
    QCOMPARE(flat(updated), QList<qint64>({ mp3 }));
    QVERIFY(!lib->refresh(9999));
    QFile::remove(f.path(s(u"a/basic.flac")));
    QVERIFY(!lib->refresh(flac)); // the file is gone: nothing changes
    QCOMPARE(lib->track(flac)->tags.fields.value(s(u"Album")), QStringList({ s(u"Flac Album") }));
    updated.clear();

    // setPath: the file was moved; the track keeps its id and everything else.
    QVERIFY(QFile::rename(f.path(s(u"b/basic.m4a")), f.path(s(u"a/moved.m4a"))));
    QVERIFY(lib->setPath(m4a, f.root + s(u"/a/x/../moved.m4a")));
    QCOMPARE(lib->track(m4a)->path, f.path(s(u"a/moved.m4a")));
    QCOMPARE(lib->trackByPath(f.path(s(u"a/moved.m4a")))->id, m4a);
    QVERIFY(!lib->trackByPath(f.path(s(u"b/basic.m4a"))));
    QCOMPARE(lib->track(m4a)->dateAdded, kNow);
    QCOMPARE(flat(updated), QList<qint64>({ m4a }));
    QVERIFY(!lib->setPath(m4a, f.path(s(u"a/basic.mp3")))); // another track is there
    QVERIFY(!lib->setPath(9999, f.path(s(u"a/zzz.mp3"))));
    QCOMPARE(lib->track(m4a)->path, f.path(s(u"a/moved.m4a")));

    // remove: forgets the tracks; no file is touched.
    QCOMPARE(lib->remove({ mp3, 9999, m4a }), 2);
    QCOMPARE(lib->count(), 1);
    QVERIFY(!lib->track(mp3));
    QVERIFY(QFileInfo::exists(f.path(s(u"a/basic.mp3"))));
    QVERIFY(QFileInfo::exists(f.path(s(u"a/moved.m4a"))));
    QCOMPARE(removed.size(), 1);
    QCOMPARE(flat(removed), sorted({ mp3, m4a }));
    QCOMPARE(lib->remove({ mp3 }), 0);
    QCOMPARE(removed.size(), 1);
    // L-ADD: an id is never used again.
    const qint64 again = lib->addFile(f.path(s(u"a/basic.mp3")), s(u"Music"), kNow);
    QVERIFY(again > m4a && again > mp3 && again > flac);
}

void LibraryTest::statistics()
{
    // L-STATS
    Folder f;
    auto lib = Library::open(s(u":memory:"));
    QVERIFY(lib);
    const qint64 id = lib->addFile(f.path(s(u"a/basic.mp3")), s(u"Music"), kNow);
    QSignalSpy updated(lib.get(), &Library::tracksUpdated);
    QVERIFY(lib->recordPlay(id, utc(2026, 11, 14, 9, 0, 0)));
    QVERIFY(lib->recordPlay(id, QDateTime(QDate(2026, 11, 15), QTime(1, 0), QTimeZone::fromSecondsAheadOfUtc(7200))));
    QCOMPARE(lib->track(id)->playCount, 2);
    QCOMPARE(lib->track(id)->lastPlayed, utc(2026, 11, 14, 23, 0, 0)); // 01:00 at UTC+2
    QCOMPARE(lib->track(id)->lastPlayed.offsetFromUtc(), 0);
    QVERIFY(lib->track(id)->lastPlayed.timeSpec() == Qt::UTC);
    // An earlier play still counts but does not move "last played" back.
    QVERIFY(lib->recordPlay(id, utc(2026, 11, 1)));
    QCOMPARE(lib->track(id)->playCount, 3);
    QCOMPARE(lib->track(id)->lastPlayed, utc(2026, 11, 14, 23, 0, 0));
    QVERIFY(lib->recordSkip(id, utc(2026, 11, 16)));
    QCOMPARE(lib->track(id)->skipCount, 1);
    QCOMPARE(lib->track(id)->playCount, 3);
    QCOMPARE(lib->track(id)->lastPlayed, utc(2026, 11, 14, 23, 0, 0)); // a skip is not a play
    QCOMPARE(updated.size(), 4);
    QVERIFY(!lib->recordPlay(9999, kNow));
    QVERIFY(!lib->recordSkip(9999, kNow));
    QVERIFY(!lib->recordPlay(id, QDateTime()));
    QCOMPARE(lib->track(id)->playCount, 3);
    QCOMPARE(updated.size(), 4);

    // setStatistics replaces all four values at once (an import from another program).
    linuxbee::TrackStatistics st;
    st.dateAdded = QDateTime(QDate(2019, 3, 4), QTime(7, 0, 0), QTimeZone::fromSecondsAheadOfUtc(3600));
    st.playCount = 41;
    st.skipCount = 7;
    st.lastPlayed = utc(2025, 12, 31, 23, 59, 59);
    QVERIFY(lib->setStatistics(id, st));
    QCOMPARE(lib->track(id)->dateAdded, utc(2019, 3, 4, 6, 0, 0));
    QCOMPARE(lib->track(id)->playCount, 41);
    QCOMPARE(lib->track(id)->skipCount, 7);
    QCOMPARE(lib->track(id)->lastPlayed, utc(2025, 12, 31, 23, 59, 59));
    QCOMPARE(updated.size(), 5);
    st.lastPlayed = QDateTime(); // never played
    st.playCount = 0;
    QVERIFY(lib->setStatistics(id, st));
    QVERIFY(!lib->track(id)->lastPlayed.isValid());
    QCOMPARE(lib->track(id)->playCount, 0);
    QCOMPARE(lib->track(id)->tags.fields.value(s(u"Title")), QStringList({ s(u"Someone like You") })); // nothing else moves
    // Refused, changing nothing: an unknown id, no date added, a negative count.
    QVERIFY(!lib->setStatistics(9999, st));
    linuxbee::TrackStatistics bad = st;
    bad.dateAdded = QDateTime();
    QVERIFY(!lib->setStatistics(id, bad));
    bad = st;
    bad.playCount = -1;
    QVERIFY(!lib->setStatistics(id, bad));
    bad = st;
    bad.skipCount = -1;
    QVERIFY(!lib->setStatistics(id, bad));
    QCOMPARE(lib->track(id)->skipCount, 7);
    QCOMPARE(updated.size(), 6);
}

void LibraryTest::relink()
{
    // L-EDIT relinkPaths: the music folder was moved or mounted elsewhere.
    Folder f;
    auto lib = Library::open(s(u":memory:"));
    QVERIFY(lib);
    lib->scan({ f.root }, at(kNow));
    const qint64 m4a = lib->trackByPath(f.path(s(u"b/basic.m4a")))->id;
    const QString moved = f.dir.path() + QStringLiteral("/moved");
    QVERIFY(QDir().rename(f.root + s(u"/a"), moved));
    QSignalSpy updated(lib.get(), &Library::tracksUpdated);
    QCOMPARE(lib->relinkPaths(f.root + s(u"/a/"), moved), 2); // a trailing slash is ignored
    QCOMPARE(updated.size(), 1);
    QVERIFY(lib->trackByPath(moved + s(u"/basic.mp3")));
    QVERIFY(lib->trackByPath(moved + s(u"/basic.flac")));
    QVERIFY(!lib->trackByPath(f.path(s(u"a/basic.mp3"))));
    QCOMPARE(lib->track(m4a)->path, f.path(s(u"b/basic.m4a")));
    QVERIFY(!lib->trackByPath(moved + s(u"/basic.mp3"))->missing);
    // The prefix is a folder, not a piece of text: ".../b" does not match ".../bb".
    QCOMPARE(lib->relinkPaths(f.root + s(u"/"), f.root + s(u"/elsewhere")), 1); // only b/basic.m4a
    QCOMPARE(lib->track(m4a)->path, f.root + s(u"/elsewhere/b/basic.m4a"));
    QVERIFY(lib->track(m4a)->missing); // L-MISSING: nothing is at the new place
    QCOMPARE(lib->relinkPaths(f.dir.path() + s(u"/mov"), s(u"/x")), 0);
    QCOMPARE(lib->relinkPaths(QString(), s(u"/x")), 0);
    QCOMPARE(lib->relinkPaths(moved, QString()), 0);
    QCOMPARE(lib->count(), 3);
}

void LibraryTest::fields()
{
    // L-FIELDS
    QCOMPARE(linuxbee::libraryFieldNames(),
        QStringList({ s(u"Date Added"), s(u"Date Modified"), s(u"Last Played"), s(u"Play Count"),
            s(u"Skip Count"), s(u"Category"), s(u"Size") }));
    Folder f;
    auto lib = Library::open(s(u":memory:"));
    QVERIFY(lib);
    const qint64 id = lib->addFile(f.path(s(u"a/basic.flac")), s(u"Inbox"), kNow);
    LibraryTrack t = *lib->track(id);
    {
        const linuxbee::LibraryFieldSource source(t);
        QCOMPARE(source.value(s(u"Date Added")), s(u"2026-11-13T12:00:00"));
        QCOMPARE(source.value(s(u"date added")), s(u"2026-11-13T12:00:00")); // names ignore case
        QCOMPARE(source.value(s(u"Date Modified")), s(u"2020-01-02T03:04:05"));
        QCOMPARE(source.value(s(u"Last Played")), QString()); // never: known and blank
        QCOMPARE(source.value(s(u"Play Count")), s(u"0"));
        QCOMPARE(source.value(s(u"Skip Count")), s(u"0"));
        QCOMPARE(source.value(s(u"Category")), s(u"Inbox"));
        QCOMPARE(source.value(s(u"Size")), s(u"0.0 MB")); // 1724 bytes
        // Everything a file's own fields give is still there.
        const linuxbee::TrackFieldSource plain(t.tags, t.path);
        for (const char16_t *name : { u"Title", u"Artist", u"Album Artist", u"Disc-Track#", u"Filename", u"Genre" })
            QCOMPARE(source.value(s(name)), plain.value(s(name)));
        QCOMPARE(source.value(s(u"Title")), s(u"Flac Title"));
        QVERIFY(!source.value(s(u"No Such Field")));
        QCOMPARE(linuxbee::Template::parse(s(u"<Category>: <Title> [<Play Count>]")).evaluate(source),
            s(u"Inbox: Flac Title [0]"));
    }
    t.sizeBytes = 5 * 1048576 + 524288; // 5.5 MB
    t.playCount = 12;
    t.skipCount = 3;
    t.lastPlayed = QDateTime(QDate(2026, 11, 15), QTime(1, 0, 9), QTimeZone::fromSecondsAheadOfUtc(7200));
    const linuxbee::LibraryFieldSource source(t);
    QCOMPARE(source.value(s(u"Size")), s(u"5.5 MB"));
    QCOMPARE(source.value(s(u"Play Count")), s(u"12"));
    QCOMPARE(source.value(s(u"Skip Count")), s(u"3"));
    QCOMPARE(source.value(s(u"Last Played")), s(u"2026-11-14T23:00:09")); // in UTC
}

void LibraryTest::refusals()
{
    Folder f;
    auto lib = Library::open(s(u":memory:"));
    QVERIFY(lib);
    // L-SCAN: no valid "now", or a category that does not exist: nothing happens.
    linuxbee::ScanOptions noNow;
    QCOMPARE(lib->scan({ f.root }, noNow).added.size(), 0);
    QCOMPARE(lib->scan({ f.root }, at(kNow, s(u"Stuff"))).added.size(), 0);
    QCOMPARE(lib->count(), 0);
    QCOMPARE(lib->addFile(f.path(s(u"a/basic.mp3")), s(u"Music"), QDateTime()), qint64(0));
    QCOMPARE(lib->remove({ }), 0);
    QCOMPARE(lib->setCategory({ }, s(u"Music")), 0);
    QVERIFY(lib->tracks(s(u"Stuff")).isEmpty());
}

QTEST_GUILESS_MAIN(LibraryTest)
#include "tst_library.moc"
