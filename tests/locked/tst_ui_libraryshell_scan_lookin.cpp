// LOCKED ACCEPTANCE TEST (the scan line says what could not be looked in, and what was passed over;
// fix-d3). Written by the reviewer, not the builder. The builder makes these pass by changing src/,
// never by editing this file. A test believed to be wrong is reported, with the reason, and left
// alone.
//
// Expected values are rules in docs/spec/library-shell.md (SH-MESSAGE) and library.md (L-LOOKIN).
#include <QAction>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include <unistd.h>

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

class LibraryShellScanLookInTest : public QObject {
    Q_OBJECT
private slots:
    void aFolderThatCannotBeLookedInIsNamed();
    void twoFoldersAreCounted();
    void filesStoredMeanwhileArePassedOver();
};

namespace {

bool scanned(Rig &r)
{
    QSignalSpy finished(r.shell->scanner(), &ScanRunner::finished);
    r.scan();
    return finished.wait(10000) || finished.size() == 1;
}

void locked(const QString &folder, bool shut)
{
    QFile::setPermissions(folder,
        shut ? QFileDevice::Permissions() : QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}

}

void LibraryShellScanLookInTest::aFolderThatCannotBeLookedInIsNamed()
{
    // SH-MESSAGE, L-LOOKIN: a folder the scan cannot look in is named on the scan's line; its
    // tracks are left as they are.
    if (::geteuid() == 0)
        QSKIP("root may look in any folder");
    Rig r;
    const QString b = r.made(QStringLiteral("music/b"));
    QVERIFY(QFile::copy(
        QStringLiteral(LINUXBEE_TEST_DATA "/tags/basic.mp3"), b + QStringLiteral("/two.mp3")));
    QVERIFY(scanned(r));
    QTRY_VERIFY(!r.shell->scanner()->isRunning());
    QCOMPARE(
        r.shell->lastMessage(), QStringLiteral("Scan: 2 added, 0 updated, 0 missing, 0 not audio"));
    locked(b, true);
    QVERIFY(scanned(r));
    QTRY_VERIFY(!r.shell->scanner()->isRunning());
    locked(b, false);
    QCOMPARE(r.shell->lastMessage(),
        QStringLiteral("Scan: 0 added, 0 updated, 0 missing, 0 not audio; could not look in ") + b
            + QStringLiteral(", and its tracks are left as they are"));
}

void LibraryShellScanLookInTest::twoFoldersAreCounted()
{
    // SH-MESSAGE: two or more such folders are counted, not named.
    if (::geteuid() == 0)
        QSKIP("root may look in any folder");
    Rig r;
    const QString b = r.made(QStringLiteral("music/b"));
    const QString c = r.made(QStringLiteral("music/c"));
    QVERIFY(QFile::copy(
        QStringLiteral(LINUXBEE_TEST_DATA "/tags/basic.mp3"), b + QStringLiteral("/two.mp3")));
    QVERIFY(QFile::copy(
        QStringLiteral(LINUXBEE_TEST_DATA "/tags/basic.ogg"), c + QStringLiteral("/three.ogg")));
    QVERIFY(scanned(r));
    QTRY_VERIFY(!r.shell->scanner()->isRunning());
    locked(b, true);
    locked(c, true);
    QVERIFY(scanned(r));
    QTRY_VERIFY(!r.shell->scanner()->isRunning());
    locked(b, false);
    locked(c, false);
    QCOMPARE(r.shell->lastMessage(),
        QStringLiteral(
            "Scan: 0 added, 0 updated, 0 missing, 0 not audio; could not look in 2 folders, "
            "and their tracks are left as they are"));
}

void LibraryShellScanLookInTest::filesStoredMeanwhileArePassedOver()
{
    // SH-MESSAGE, L-LOOKIN "Held by now": a file the scan found new, which another writer stored
    // while it looked, is passed over, and the line says so instead of "0 added" alone.
    Rig r;
    bool once = false;
    QObject::connect(r.shell->scanner(), &ScanRunner::progress, r.shell.get(), [&r, &once] {
        if (once)
            return;
        once = true;
        ScanOptions o;
        o.category = QStringLiteral("Music");
        o.now = r.now;
        (void)r.library->scan(r.folders, o); // another writer, between the looking and the storing
    });
    QSignalSpy finished(r.shell->scanner(), &ScanRunner::finished);
    r.scan();
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
    QTRY_VERIFY(!r.shell->scanner()->isRunning());
    const ScanReport report = finished.at(0).at(0).value<ScanReport>();
    QVERIFY2(report.passedOver == 1, qPrintable(QString::number(report.passedOver)));
    QCOMPARE(r.library->tracks().size(), 1);
    QCOMPARE(r.shell->lastMessage(),
        QStringLiteral("Scan: 0 added, 0 updated, 0 missing, 0 not audio; 1 passed over, changed "
                       "meanwhile: the next scan looks again"));
}

QTEST_MAIN(LibraryShellScanLookInTest)
#include "tst_ui_libraryshell_scan_lookin.moc"
