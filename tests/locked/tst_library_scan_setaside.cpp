// LOCKED ACCEPTANCE TEST (what Organise set aside is put back by a scan; fix-h1). Written by the
// reviewer, not the builder. The builder makes these pass by changing src/, never by editing this
// file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values: docs/spec/library.md (L-SETASIDE) and organise.md (O-REPLACED). Organise F7 of
// the audit: a kill between the set-aside and the trash left the owner's old file in a hidden
// `.linuxbee-replaced-N/` folder that nothing looked at again. The orchestrator's ruling: such a
// file may be the only copy, so it is never removed. It is put back under its name, never over
// anything, or kept and named. What is said is checked by cli_library_scan_setaside.sh and
// tst_ui_libraryshell_scan_setaside; this test looks at the disk. LINUXBEE_LEFTOVER_AGE is set to 0
// here so that a file just made counts as old, except in the row that checks the age. Every file is
// made here.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

#include "library/library.h"

using namespace linuxbee;

namespace {

const QDateTime kNow = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);

ScanOptions options()
{
    ScanOptions o;
    o.category = QStringLiteral("Music");
    o.now = kNow;
    return o;
}

QByteArray bytesOf(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArrayLiteral("<cannot open>");
}

bool put(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

// A real music file at `path`: a copy of a fixture.
bool music(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    return QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/basic.flac"), path);
}

}

class LibraryScanSetAsideTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void aFileSetAsideIsPutBack();
    void bothAreKeptAndNamed();
    void aDanglingLinkAtTheNameIsNotReplaced();
    void aFreshSetAsideIsLeftAlone();
    void onlyOurOwnNames();
    void onlyTheFoldersGiven();
    void theMonitorsScanDoesNotSweep();

private:
    QTemporaryDir *m_dir = nullptr;
    QString m_music;
    std::unique_ptr<Library> m_library;
};

void LibraryScanSetAsideTest::init()
{
    qputenv("LINUXBEE_LEFTOVER_AGE", "0");
    m_dir = new QTemporaryDir;
    QVERIFY(m_dir->isValid());
    m_music = QFileInfo(m_dir->path()).canonicalFilePath() + QStringLiteral("/music");
    QVERIFY(QDir().mkpath(m_music + QStringLiteral("/a")));
    m_library = Library::open(m_dir->path() + QStringLiteral("/library.db"));
    QVERIFY(m_library);
}

void LibraryScanSetAsideTest::cleanup()
{
    m_library.reset();
    delete m_dir;
    m_dir = nullptr;
    qunsetenv("LINUXBEE_LEFTOVER_AGE");
}

void LibraryScanSetAsideTest::aFileSetAsideIsPutBack()
{
    // The real name is free: the file is put back under it, said, and found by the same scan. The
    // waiting folder, empty now, goes.
    const QString waiting = m_music + QStringLiteral("/a/.linuxbee-replaced-1");
    QVERIFY(music(waiting + QStringLiteral("/x.flac")));
    const QByteArray before = bytesOf(waiting + QStringLiteral("/x.flac"));
    const ScanReport r = m_library->scan({ m_music }, options());
    QCOMPARE(bytesOf(m_music + QStringLiteral("/a/x.flac")), before);
    QVERIFY(!QFileInfo::exists(waiting));
    QCOMPARE(r.added.size(), 1);
    QVERIFY(m_library->trackByPath(m_music + QStringLiteral("/a/x.flac")).has_value());
}

void LibraryScanSetAsideTest::bothAreKeptAndNamed()
{
    // The real name holds another file (the newcomer): both stay, byte for byte, and the set-aside
    // one is named. Its waiting folder stays.
    const QString waiting = m_music + QStringLiteral("/a/.linuxbee-replaced-2");
    QVERIFY(put(waiting + QStringLiteral("/x.flac"), "the old one"));
    QVERIFY(put(m_music + QStringLiteral("/a/x.flac"), "the newcomer"));
    QVERIFY(m_library->scan({ m_music }, options()).refused.isEmpty());
    QCOMPARE(bytesOf(waiting + QStringLiteral("/x.flac")), QByteArray("the old one"));
    QCOMPARE(bytesOf(m_music + QStringLiteral("/a/x.flac")), QByteArray("the newcomer"));
}

void LibraryScanSetAsideTest::aDanglingLinkAtTheNameIsNotReplaced()
{
    // The orchestrator's race row: a name taken between a look and the put-back must not be
    // written over. A link that leads nowhere is "not there" to a look by exists(), and is there to
    // the never-replacing call. It stays a link; the set-aside file stays and is named.
    const QString waiting = m_music + QStringLiteral("/a/.linuxbee-replaced-3");
    QVERIFY(put(waiting + QStringLiteral("/y.flac"), "the old one"));
    QVERIFY(QFile::link(
        m_dir->path() + QStringLiteral("/nowhere"), m_music + QStringLiteral("/a/y.flac")));
    QVERIFY(m_library->scan({ m_music }, options()).refused.isEmpty());
    QVERIFY(QFileInfo(m_music + QStringLiteral("/a/y.flac")).isSymLink());
    QCOMPARE(bytesOf(waiting + QStringLiteral("/y.flac")), QByteArray("the old one"));
}

void LibraryScanSetAsideTest::aFreshSetAsideIsLeftAlone()
{
    // The age (600 seconds when nothing else is named): a file set aside just now may be an
    // Organise at work. It is left where it is, and not said.
    qunsetenv("LINUXBEE_LEFTOVER_AGE");
    const QString waiting = m_music + QStringLiteral("/a/.linuxbee-replaced-1");
    QVERIFY(put(waiting + QStringLiteral("/x.flac"), "the old one"));
    QVERIFY(m_library->scan({ m_music }, options()).refused.isEmpty());
    QCOMPARE(bytesOf(waiting + QStringLiteral("/x.flac")), QByteArray("the old one"));
    QVERIFY(!QFileInfo::exists(m_music + QStringLiteral("/a/x.flac")));
}

void LibraryScanSetAsideTest::onlyOurOwnNames()
{
    // Only the waiting folder's name exactly as Organise makes it, only regular files directly in
    // it, and never through a link.
    const QString a = m_music + QStringLiteral("/a");
    for (const char *name : { "/.linuxbee-replaced-01/p.flac", "/.linuxbee-replaced-1x/q.flac",
             "/.linuxbee-replaced-/r.flac", "/linuxbee-replaced-1/s.flac",
             "/.linuxbee-replaced-10000/t.flac", "/.linuxbee-replaced-5/sub/u.flac" })
        QVERIFY(put(a + QLatin1String(name), "not ours"));
    // A link named like a waiting folder, to a folder elsewhere holding a file.
    QVERIFY(put(m_dir->path() + QStringLiteral("/elsewhere/v.flac"), "elsewhere"));
    QVERIFY(QFile::link(
        m_dir->path() + QStringLiteral("/elsewhere"), a + QStringLiteral("/.linuxbee-replaced-6")));
    // A link inside a real waiting folder.
    QVERIFY(QDir().mkpath(a + QStringLiteral("/.linuxbee-replaced-7")));
    QVERIFY(QFile::link(m_dir->path() + QStringLiteral("/elsewhere/v.flac"),
        a + QStringLiteral("/.linuxbee-replaced-7/w.flac")));
    // A waiting folder inside a hidden folder is not gone into.
    QVERIFY(put(a + QStringLiteral("/.hidden/.linuxbee-replaced-1/z.flac"), "not looked at"));
    QVERIFY(m_library->scan({ m_music }, options()).refused.isEmpty());
    for (const char *name : { "p", "q", "r", "s", "t", "u", "v", "w", "z" })
        QVERIFY2(!QFileInfo::exists(
                     a + QLatin1Char('/') + QLatin1String(name) + QStringLiteral(".flac")),
            name);
    QCOMPARE(bytesOf(m_dir->path() + QStringLiteral("/elsewhere/v.flac")), QByteArray("elsewhere"));
    QVERIFY(QFileInfo(a + QStringLiteral("/.linuxbee-replaced-7/w.flac")).isSymLink());
    QCOMPARE(bytesOf(a + QStringLiteral("/.hidden/.linuxbee-replaced-1/z.flac")),
        QByteArray("not looked at"));
}

void LibraryScanSetAsideTest::onlyTheFoldersGiven()
{
    // A scan of `a` does not look in `b`, its sibling.
    const QString waiting = m_music + QStringLiteral("/b/.linuxbee-replaced-1");
    QVERIFY(put(waiting + QStringLiteral("/x.flac"), "the old one"));
    QVERIFY(m_library->scan({ m_music + QStringLiteral("/a") }, options()).refused.isEmpty());
    QVERIFY(QFileInfo::exists(waiting + QStringLiteral("/x.flac")));
    QVERIFY(!QFileInfo::exists(m_music + QStringLiteral("/b/x.flac")));
}

void LibraryScanSetAsideTest::theMonitorsScanDoesNotSweep()
{
    // L-WAIT, L-SETASIDE: a scan nobody asked for (the folder monitor's) has no line to say it on,
    // so it leaves the set-aside file where it is.
    const QString waiting = m_music + QStringLiteral("/a/.linuxbee-replaced-1");
    QVERIFY(put(waiting + QStringLiteral("/x.flac"), "the old one"));
    ScanOptions o = options();
    o.nobodyWaits = true;
    QVERIFY(m_library->scan({ m_music }, o).refused.isEmpty());
    QVERIFY(QFileInfo::exists(waiting + QStringLiteral("/x.flac")));
}

QTEST_GUILESS_MAIN(LibraryScanSetAsideTest)
#include "tst_library_scan_setaside.moc"
