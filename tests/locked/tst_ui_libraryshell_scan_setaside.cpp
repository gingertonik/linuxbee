// LOCKED ACCEPTANCE TEST (the scan line says what Organise had set aside; fix-h1). Written
// by the reviewer, not the builder. The builder makes these pass by changing src/, never by editing
// this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/library-shell.md (SH-MESSAGE) and library.md
// (L-SETASIDE). The library, the music and the settings are this test's own, in a temporary folder.
#include <QAction>
#include <QFile>
#include <QFileInfo>
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

class LibraryShellScanSetAsideTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void putBackIsSaid();
    void keptIsSaid();
};

void LibraryShellScanSetAsideTest::initTestCase()
{
    qputenv("LINUXBEE_LEFTOVER_AGE", "0");
}

void LibraryShellScanSetAsideTest::putBackIsSaid()
{
    // SH-MESSAGE, L-SETASIDE: "Scan Folders" puts back a file Organise had set aside, names it, and
    // counts it as added.
    Rig r;
    const QString waiting = r.made(QStringLiteral("music/.linuxbee-replaced-1"));
    QVERIFY(QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/basic.mp3"),
        waiting + QStringLiteral("/old.mp3")));
    QSignalSpy finished(r.shell->scanner(), &ScanRunner::finished);
    r.scan();
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
    QTRY_VERIFY(!r.shell->scanner()->isRunning());
    QCOMPARE(r.shell->lastMessage(),
        QStringLiteral("Scan: 2 added, 0 updated, 0 missing, 0 not audio; put back ")
            + r.folders.first() + QStringLiteral("/old.mp3, which Organise had set aside"));
    QVERIFY(QFileInfo::exists(r.folders.first() + QStringLiteral("/old.mp3")));
}

void LibraryShellScanSetAsideTest::keptIsSaid()
{
    // When a file has the name, both stay and the line names both.
    Rig r;
    const QString waiting = r.made(QStringLiteral("music/.linuxbee-replaced-1"));
    QVERIFY(QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/basic.flac"),
        waiting + QStringLiteral("/a.flac")));
    QSignalSpy finished(r.shell->scanner(), &ScanRunner::finished);
    r.scan();
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
    QTRY_VERIFY(!r.shell->scanner()->isRunning());
    QCOMPARE(r.shell->lastMessage(),
        QStringLiteral("Scan: 1 added, 0 updated, 0 missing, 0 not audio; kept ") + waiting
            + QStringLiteral("/a.flac, which Organise had set aside, beside ") + r.folders.first()
            + QStringLiteral("/a.flac"));
    QVERIFY(QFileInfo::exists(waiting + QStringLiteral("/a.flac")));
}

QTEST_MAIN(LibraryShellScanSetAsideTest)
#include "tst_ui_libraryshell_scan_setaside.moc"
