// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
//
// L-STORE: "the clean absolute path (unique)". L-SCAN: a known track "whose
// path lies inside one of the scanned folders, and whose file no longer
// exists is marked missing". A folder or file given relative to the current
// folder names the same place as its absolute path.
// L-SIGNALS: "A call that changes nothing emits nothing."
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "library/library.h"

using linuxbee::Library;

namespace {

const QDateTime kNow(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);

QString fixture(const QString &name)
{
    return QStringLiteral(LINUXBEE_TEST_DATA "/tags/") + name;
}

linuxbee::ScanOptions at(const QDateTime &now)
{
    linuxbee::ScanOptions options;
    options.now = now;
    return options;
}

// Runs the test with `folder` as the current folder, and puts the old one back.
struct InFolder {
    QString old = QDir::currentPath();
    explicit InFolder(const QString &folder) { QDir::setCurrent(folder); }
    ~InFolder() { QDir::setCurrent(old); }
};

}

class LibraryPathsTest : public QObject {
    Q_OBJECT
private slots:
    void relativeScanFolderMarksMissing()
    {
        QTemporaryDir dir;
        const QString root = QFileInfo(dir.path()).canonicalFilePath();
        QVERIFY(QDir().mkpath(root + QStringLiteral("/m")));
        QVERIFY(QFile::copy(fixture(QStringLiteral("basic.mp3")), root + QStringLiteral("/m/a.mp3")));
        QVERIFY(QFile::copy(fixture(QStringLiteral("basic.flac")), root + QStringLiteral("/m/b.flac")));
        InFolder here(root);
        auto lib = Library::open(QStringLiteral(":memory:"));
        QVERIFY(lib);
        const auto first = lib->scan({ QStringLiteral("m") }, at(kNow));
        QCOMPARE(first.added.size(), 2);
        const auto a = lib->trackByPath(root + QStringLiteral("/m/a.mp3"));
        QVERIFY(a);
        QVERIFY(QFile::remove(root + QStringLiteral("/m/a.mp3")));
        const auto again = lib->scan({ QStringLiteral("m") }, at(kNow));
        QCOMPARE(again.nowMissing, QList<qint64> { a->id });
        QCOMPARE(again.unchanged, 1);
        QVERIFY(lib->track(a->id)->missing);
        // The same through "./m/" and through "m/../m".
        QVERIFY(QFile::copy(fixture(QStringLiteral("basic.mp3")), root + QStringLiteral("/m/a.mp3")));
        const auto back = lib->scan({ QStringLiteral("./m/") }, at(kNow));
        QCOMPARE(back.found, QList<qint64> { a->id });
        QVERIFY(QFile::remove(root + QStringLiteral("/m/a.mp3")));
        const auto gone = lib->scan({ QStringLiteral("m/../m") }, at(kNow));
        QCOMPARE(gone.nowMissing, QList<qint64> { a->id });
    }

    void relativePathsAreStoredAbsolute()
    {
        QTemporaryDir dir;
        const QString root = QFileInfo(dir.path()).canonicalFilePath();
        QVERIFY(QDir().mkpath(root + QStringLiteral("/m")));
        QVERIFY(QFile::copy(fixture(QStringLiteral("basic.m4a")), root + QStringLiteral("/m/c.m4a")));
        QVERIFY(QFile::copy(fixture(QStringLiteral("basic.mp3")), root + QStringLiteral("/m/d.mp3")));
        InFolder here(root);
        auto lib = Library::open(QStringLiteral(":memory:"));
        QVERIFY(lib);
        const qint64 id = lib->addFile(QStringLiteral("m/c.m4a"), QStringLiteral("Music"), kNow);
        QVERIFY(id > 0);
        QCOMPARE(lib->track(id)->path, root + QStringLiteral("/m/c.m4a"));
        QCOMPARE(lib->trackByPath(QStringLiteral("m/c.m4a"))->id, id);
        QCOMPARE(lib->trackByPath(root + QStringLiteral("/m/c.m4a"))->id, id);
        // The same file, given absolute and given relative, is one track.
        QCOMPARE(lib->addFile(root + QStringLiteral("/m/c.m4a"), QStringLiteral("Music"), kNow), id);
        const auto scanned = lib->scan({ root + QStringLiteral("/m") }, at(kNow));
        QCOMPARE(scanned.added.size(), 1); // d.mp3 only
        QCOMPARE(scanned.unchanged, 1);
        QCOMPARE(lib->count(), 2);
        // setPath with a relative path stores it absolute.
        QVERIFY(QFile::rename(root + QStringLiteral("/m/c.m4a"), root + QStringLiteral("/c.m4a")));
        QVERIFY(lib->setPath(id, QStringLiteral("c.m4a")));
        QCOMPARE(lib->track(id)->path, root + QStringLiteral("/c.m4a"));
        QVERIFY(!lib->track(id)->missing);
        // relinkPaths with relative prefixes works on the absolute paths.
        const qint64 d = lib->trackByPath(root + QStringLiteral("/m/d.mp3"))->id;
        QVERIFY(QDir().rename(root + QStringLiteral("/m"), root + QStringLiteral("/n")));
        QCOMPARE(lib->relinkPaths(QStringLiteral("m"), QStringLiteral("n/")), 1);
        QCOMPARE(lib->track(d)->path, root + QStringLiteral("/n/d.mp3"));
        QVERIFY(!lib->track(d)->missing);
    }

    void setPathToItsOwnPathChangesNothing()
    {
        QTemporaryDir dir;
        const QString root = QFileInfo(dir.path()).canonicalFilePath();
        QVERIFY(QFile::copy(fixture(QStringLiteral("basic.mp3")), root + QStringLiteral("/a.mp3")));
        auto lib = Library::open(QStringLiteral(":memory:"));
        QVERIFY(lib);
        const qint64 id = lib->addFile(root + QStringLiteral("/a.mp3"), QStringLiteral("Music"), kNow);
        QVERIFY(id > 0);
        QSignalSpy updated(lib.get(), &Library::tracksUpdated);
        QVERIFY(lib->setPath(id, root + QStringLiteral("/a.mp3")));
        QCOMPARE(updated.count(), 0);
        QCOMPARE(lib->track(id)->path, root + QStringLiteral("/a.mp3"));
    }
};

QTEST_GUILESS_MAIN(LibraryPathsTest)
#include "tst_library_paths.moc"
