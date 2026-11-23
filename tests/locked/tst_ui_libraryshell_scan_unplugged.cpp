// LOCKED ACCEPTANCE TEST (the scan line says a library folder looks unplugged; fix-m1). Written
// by the reviewer, not the builder. The builder makes these pass by changing src/, never by editing
// this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/library-shell.md (SH-MESSAGE) and library.md
// (L-UNPLUGGED).
#include <QAction>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include "actions/actionregistry.h"
#include "library/library.h"
#include "mainwindow/mainwindow.h"
#include "playlists/playliststore.h"
#include "shell/libraryshell.h"
#include "shell/scanrunner.h"

using namespace linuxbee;
using namespace linuxbee::ui;

namespace {

struct Rig {
    QTemporaryDir dir;
    QString file;
    std::unique_ptr<Library> library;
    PlaylistStore playlists;
    QSettings settings;
    ActionRegistry actions;
    MainWindow window;
    QDateTime now = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);
    std::unique_ptr<LibraryShell> shell;
    QStringList folders;

    Rig()
        : file(dir.path() + QStringLiteral("/library.db"))
        , library(Library::open(file))
        , playlists(made(QStringLiteral("playlists")))
        , settings(dir.path() + QStringLiteral("/s.ini"), QSettings::IniFormat)
        , window(&actions, &settings)
    {
        made(QStringLiteral("music"));
        if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/basic.flac"),
                dir.path() + QStringLiteral("/music/a.flac")))
            QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
        folders = { dir.path() + QStringLiteral("/music") };
        ShellHooks hooks;
        hooks.now = [this] { return now; };
        hooks.chooseFolders = [this] { return folders; };
        shell = std::make_unique<LibraryShell>(
            &window, &actions, library.get(), &playlists, &settings, hooks);
    }
    QString made(const QString &name)
    {
        QDir(dir.path()).mkpath(name);
        return dir.path() + QLatin1Char('/') + name;
    }
    void scan()
    {
        if (QAction *action = actions.action(QStringLiteral("file.scanFolders")))
            action->trigger();
        else
            QTest::qFail("no such action", __FILE__, __LINE__);
    }
};

}

class LibraryShellScanUnpluggedTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void anEmptiedFolderIsSaidToLookUnplugged();

private:
    QTemporaryDir tables;
};

void LibraryShellScanUnpluggedTest::initTestCase()
{
    QFile empty(tables.path() + QStringLiteral("/fstab"));
    QVERIFY(empty.open(QIODevice::WriteOnly));
    qputenv("LINUXBEE_FSTAB", QFile::encodeName(empty.fileName()));
}

void LibraryShellScanUnpluggedTest::anEmptiedFolderIsSaidToLookUnplugged()
{
    // SH-MESSAGE, L-UNPLUGGED: "Scan Folders" on a library folder that now holds no music, where
    // its track was, says it looks unplugged; the track is not marked missing, and the line does
    // not count it as missing.
    Rig r;
    QSignalSpy finished(r.shell->scanner(), &ScanRunner::finished);
    r.scan();
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
    QTRY_VERIFY(!r.shell->scanner()->isRunning());
    QCOMPARE(
        r.shell->lastMessage(), QStringLiteral("Scan: 1 added, 0 updated, 0 missing, 0 not audio"));
    QVERIFY(QFile::remove(r.dir.path() + QStringLiteral("/music/a.flac")));
    r.scan();
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 10000);
    QTRY_VERIFY(!r.shell->scanner()->isRunning());
    QCOMPARE(r.shell->lastMessage(),
        QStringLiteral("Scan: 0 added, 0 updated, 0 missing, 0 not audio; ") + r.folders.first()
            + QStringLiteral(" looks unplugged, and its tracks are left as they are"));
    for (const LibraryTrack &t : r.library->tracks())
        QVERIFY2(!t.missing, qPrintable(t.path));
}

QTEST_MAIN(LibraryShellScanUnpluggedTest)
#include "tst_ui_libraryshell_scan_unplugged.moc"
