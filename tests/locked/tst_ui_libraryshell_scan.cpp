// LOCKED ACCEPTANCE TEST (Scan Folders through the runner). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/library-shell.md (SH-x). Runs on the
// offscreen platform with a real library (in memory), a real playlist folder
// (in a temporary directory) and the real window parts.
#include <QSignalSpy>
#include <QStatusBar>
#include <QFile>
#include <QAction>
#include <QLineEdit>
#include <QSettings>
#include <QLabel>
#include <QPointer>
#include <QSignalSpy>
#include <QSplitter>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest>

#include "actions/actionregistry.h"
#include "mainwindow/mainwindow.h"
#include "navigator/navigatorpanel.h"
#include "shell/libraryshell.h"
#include "shell/scanrunner.h"
#include "tracklist/tracklistmodel.h"
#include "tracklist/tracklistpage.h"

using namespace linuxbee;
using namespace linuxbee::ui;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

// A settings file as the next run of the program reads it. Within one run Qt
// hands back the value that was put in (a bool stays a bool); read from disk
// by a later run, every value is text. Copying the file gives the later view.
QString nextRun(QSettings &settings)
{
    static int count = 0;
    settings.sync();
    const QString copy = settings.fileName() + QStringLiteral(".run%1.ini").arg(++count);
    QFile::remove(copy);
    QFile::copy(settings.fileName(), copy);
    return copy;
}

QString fixture(const char *name)
{
    return QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/") + QLatin1String(name);
}

// Stops a test, as a failure, when the shell has not built its parts.
#define NEED_PARTS(r) \
    do { \
        if (!(r).shell->navigator() || !(r).shell->model() || !(r).shell->trackList()) \
            QFAIL("the shell has not built its navigator, model and track list"); \
    } while (false)

QStringList sorted(QStringList list)
{
    list.sort();
    return list;
}

struct Rig {
    QTemporaryDir dir;
    QString a, b, c, d, junk;
    std::unique_ptr<Library> library;
    PlaylistStore playlists;
    QSettings settings;
    ActionRegistry actions;
    MainWindow window;
    QStringList folders; // what the folder chooser will answer
    QStringList files; // what the file chooser will answer
    int asked = 0;
    QDateTime now = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);
    std::unique_ptr<LibraryShell> shell;

    Rig()
        : library(Library::open(QStringLiteral(":memory:")))
        , playlists(made(QStringLiteral("playlists")))
        , settings(dir.path() + QStringLiteral("/s.ini"), QSettings::IniFormat)
        , window(&actions, &settings)
    {
        made(QStringLiteral("music"));
        made(QStringLiteral("inbox"));
        a = put("basic.flac", "music/a.flac");
        b = put("basic.mp3", "music/b.mp3");
        c = put("basic.ogg", "music/c.ogg");
        junk = put("notaudio.mp3", "music/junk.mp3");
        d = put("basic.flac", "inbox/d.flac");
    }
    QString made(const QString &name)
    {
        QDir(dir.path()).mkpath(name);
        return dir.path() + QLatin1Char('/') + name;
    }
    QString put(const char *from, const char *to)
    {
        const QString target = dir.path() + QLatin1Char('/') + QLatin1String(to);
        if (!QFile::copy(fixture(from), target))
            QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
        return target;
    }
    void fill() // three in Music, one in the Inbox
    {
        for (const QString &p : { a, b, c })
            QVERIFY(library->addFile(p, QStringLiteral("Music"), now) != 0);
        QVERIFY(library->addFile(d, QStringLiteral("Inbox"), now) != 0);
    }
    void start()
    {
        ShellHooks hooks;
        hooks.chooseFolders = [this] {
            ++asked;
            return folders;
        };
        hooks.chooseFiles = [this] {
            ++asked;
            return files;
        };
        hooks.now = [this] { return now; };
        shell = std::make_unique<LibraryShell>(&window, &actions, library.get(), &playlists, &settings, hooks);
    }
    void run(const char16_t *id)
    {
        if (QAction *action = actions.action(s(id)))
            action->trigger();
        else
            QTest::qFail("no such action", __FILE__, __LINE__);
    }
    qint64 idOf(const QString &path) const
    {
        const auto t = library->trackByPath(path);
        return t ? t->id : 0;
    }
    QStringList shown() const { return sorted(shell->shownPaths()); }
    QStringList navigatorRows() const
    {
        QStringList out;
        auto *tree = window.findChild<QTreeWidget *>(s(u"navigatorTree"));
        if (!tree)
            return { s(u"NO TREE") };
        for (QTreeWidgetItemIterator it(tree); *it; ++it)
            out.append((*it)->text(0));
        return out;
    }
};

LibraryTrack crafted(qint64 id, const char16_t *title)
{
    LibraryTrack t;
    t.id = id;
    t.path = QStringLiteral("/music/%1.flac").arg(id);
    t.category = QStringLiteral("Music");
    t.tags.format = QStringLiteral("flac");
    t.tags.fields.insert(QStringLiteral("Title"), { s(title) });
    return t;
}

QList<qint64> rows(const TrackListModel &model)
{
    QList<qint64> out;
    for (int row = 0; row < model.rowCount(); ++row)
        out.append(model.trackId(row));
    return out;
}

}

class LibraryShellScanTest : public QObject {
    Q_OBJECT
private slots:
    void scanFoldersComesBackAtOnce();
};

void LibraryShellScanTest::scanFoldersComesBackAtOnce()
{
    // SR-SHELL: "Scan Folders" goes through the shell's runner.
    // helpers of the rig this file shares with the test it grew from
    (void)&nextRun;
    (void)&crafted;
    (void)&rows;
    Rig r;
    r.start();
    NEED_PARTS(r);
    ScanRunner *scanner = r.shell->scanner();
    if (!scanner)
        QFAIL("the shell has no scan runner");
    QVERIFY(!scanner->isRunning());
    QSignalSpy finished(scanner, &ScanRunner::finished);
    QStringList whileScanning; // the shell's message at each progress
    connect(scanner, &ScanRunner::progress, this, [&](int done, int total) {
        whileScanning.append(r.shell->lastMessage() + QLatin1Char('|') + QString::number(done) + QLatin1Char('|') + QString::number(total));
    });

    QList<int> heldAtEachEnd; // what the library holds as each scan ends
    QStringList saidAtEachEnd; // and the shell's message then (the shell heard `finished` first: it connected first)
    connect(scanner, &ScanRunner::finished, this, [&] {
        heldAtEachEnd.append(r.library->count());
        saidAtEachEnd.append(r.shell->lastMessage());
    });

    r.folders = { r.dir.path() + s(u"/music") };
    r.run(u"file.scanFolders");
    QVERIFY(scanner->isRunning());
    QCOMPARE(r.library->count(), 0); // the command came back before anything was stored
    QCOMPARE(r.shell->lastMessage(), s(u"Scan started"));
    QCOMPARE(r.window.statusBar()->currentMessage(), s(u"Scan started"));
    r.run(u"file.scanFolders"); // asked for again: another waits
    QCOMPARE(scanner->waiting(), 1);
    QCOMPARE(r.asked, 2);

    // Two small scans may both end within one wait, so what the first left is read as it ends.
    QTRY_COMPARE(finished.count(), 2);
    QCOMPARE(heldAtEachEnd, (QList<int> { 3, 3 }));
    QCOMPARE(saidAtEachEnd.value(0), s(u"Scan: 3 added, 0 updated, 0 missing, 1 not audio"));
    QCOMPARE(r.shown(), sorted({ r.a, r.b, r.c }));
    QTRY_VERIFY(!scanner->isRunning());
    QCOMPARE(r.shell->lastMessage(), s(u"Scan: 0 added, 0 updated, 0 missing, 1 not audio")); // of the second
    // SR-SHELL: the shell says nothing for each file; the scan's row counts them (SP-ROW).
    QVERIFY(!whileScanning.isEmpty());
    QVERIFY(whileScanning.contains(s(u"Scan started|4|4"))); // the first scan: what the command said stays up
    for (const QString &seen : std::as_const(whileScanning)) {
        const QString said = seen.split(QLatin1Char('|')).at(0);
        QVERIFY2(said == s(u"Scan started") || said == saidAtEachEnd.value(0), qPrintable(seen));
    }

    // A stopped scan says so.
    r.folders = { r.dir.path() + s(u"/inbox") };
    r.run(u"file.scanFolders");
    scanner->cancel();
    QTRY_COMPARE(finished.count(), 3);
    QVERIFY(finished.at(2).at(0).value<ScanReport>().cancelled);
    QVERIFY2(r.shell->lastMessage().startsWith(s(u"Scan: ")) && r.shell->lastMessage().endsWith(s(u" (stopped)")),
        qPrintable(r.shell->lastMessage()));
}

QTEST_MAIN(LibraryShellScanTest)
#include "tst_ui_libraryshell_scan.moc"
