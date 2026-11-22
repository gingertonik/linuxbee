// LOCKED ACCEPTANCE TEST (L-READFAIL 3 in the tools shell: the tag editor and Undo). Written by the
// reviewer, not the builder. The builder makes these pass by changing src/, never by editing this
// file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/library.md (L-READFAIL 3) and docs/spec/tools-shell.md.
// The library is a file here; one of its pages is overwritten while the tools shell has it, so
// every read of the tracks fails from then on.
#include <QDialog>
#include <QFile>
#include <QLineEdit>
#include <QPointer>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include "actions/actionregistry.h"
#include "library/library.h"
#include "mainwindow/mainwindow.h"
#include "navigator/navigatorpanel.h"
#include "playlists/playliststore.h"
#include "shell/libraryshell.h"
#include "shell/toolsshell.h"
#include "tageditor/tageditordialog.h"
#include "tags/tagreader.h"
#include "tagundo/tagundo.h"
#include "tracklist/tracklistpage.h"

using namespace linuxbee;
using namespace linuxbee::ui;

namespace {

// L-READFAIL (how a test makes a read fail): the root page of the tracks table is overwritten
// while the library has the file open. The library's last changes are written into the file first,
// and another connection's write afterwards makes the library read the file again rather than what
// it remembers. Every read of the tracks then fails with "malformed"; the rest of the file reads.
bool damageOpen(const QString &file)
{
    const QString name = QStringLiteral("damage-open");
    bool ok = false;
    {
        QSqlDatabase raw = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        raw.setDatabaseName(file);
        if (raw.open()) {
            QSqlQuery q(raw);
            int size = 0;
            int root = 0;
            int version = 0;
            q.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"));
            if (q.exec(QStringLiteral("PRAGMA page_size")) && q.next())
                size = q.value(0).toInt();
            if (q.exec(QStringLiteral("SELECT rootpage FROM sqlite_master WHERE name = 'tracks'"))
                && q.next())
                root = q.value(0).toInt();
            if (q.exec(QStringLiteral("PRAGMA user_version")) && q.next())
                version = q.value(0).toInt();
            q.finish();
            QFile f(file);
            if (size > 0 && root > 0 && f.open(QIODevice::ReadWrite)
                && f.seek(qint64(root - 1) * size))
                ok = f.write(QByteArray(size, char(0xff))) == size;
            f.close();
            ok = ok && q.exec(QStringLiteral("PRAGMA user_version = %1").arg(version));
            q.finish();
            raw.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
    return ok;
}

struct Rig {
    QTemporaryDir dir;
    QString file;
    QString a, b;
    std::unique_ptr<Library> library;
    PlaylistStore playlists;
    QSettings settings;
    ActionRegistry actions;
    MainWindow window;
    QDateTime now = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);
    std::unique_ptr<LibraryShell> lib;
    std::unique_ptr<ToolsShell> tools;
    int presented = 0;
    std::function<void(QDialog *)> work;

    Rig()
        : file(dir.path() + QStringLiteral("/library.db"))
        , library(Library::open(file))
        , playlists(made(QStringLiteral("playlists")))
        , settings(dir.path() + QStringLiteral("/s.ini"), QSettings::IniFormat)
        , window(&actions, &settings)
    {
        made(QStringLiteral("music"));
        a = put("tags/basic.flac", "music/a.flac");
        b = put("tags/basic.mp3", "music/b.mp3");
        for (const QString &p : { a, b })
            if (library->addFile(p, QStringLiteral("Music"), now) == 0)
                QTest::qFail("a fixture did not go into the library", __FILE__, __LINE__);
        lib = std::make_unique<LibraryShell>(
            &window, &actions, library.get(), &playlists, &settings, ShellHooks { });
        tools = std::make_unique<ToolsShell>(
            &window, &actions, library.get(), lib.get(), &settings, [this](QDialog *dialog) {
                ++presented;
                if (work)
                    work(dialog);
            });
    }
    QString made(const QString &name)
    {
        QDir(dir.path()).mkpath(name);
        return dir.path() + QLatin1Char('/') + name;
    }
    QString put(const char *from, const char *to)
    {
        const QString target = dir.path() + QLatin1Char('/') + QLatin1String(to);
        if (!QFile::copy(
                QStringLiteral(LINUXBEE_TEST_DATA) + QLatin1Char('/') + QLatin1String(from),
                target))
            QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
        return target;
    }
    void run(const char *name)
    {
        if (QAction *action = actions.action(QLatin1String(name)))
            action->trigger();
        else
            QTest::qFail("no such action", __FILE__, __LINE__);
    }
    void select(const QStringList &paths)
    {
        QList<qint64> ids;
        for (const QString &p : paths)
            if (const auto t = library->trackByPath(p))
                ids.append(t->id);
        lib->trackList()->selectTracks(ids);
    }
};

void type(QDialog *dialog, const char *name, const QString &text)
{
    if (auto *edit = dialog->findChild<QLineEdit *>(QLatin1String(name))) {
        edit->setFocus();
        edit->selectAll();
        QTest::keyClicks(edit, text);
    } else {
        QTest::qFail("no such line edit", __FILE__, __LINE__);
    }
}

}

class ToolsShellReadFailTest : public QObject {
    Q_OBJECT
private slots:
    void theTagEditorIsNotOpened();
    void undoSaysTheLibraryWasNotRead();
};

void ToolsShellReadFailTest::theTagEditorIsNotOpened()
{
    // L-READFAIL 3, TS-TAGS: the selected tracks cannot be looked up. The editor is not opened
    // (it would show no track, or fewer), and the status bar says why.
    Rig r;
    QVERIFY(r.lib->navigator()->selectNode(QStringLiteral("library:Music")));
    r.select({ r.a, r.b });
    QVERIFY(damageOpen(r.file));
    r.run("file.editTags");
    QCOMPARE(r.presented, 0);
    QCOMPARE(r.tools->lastMessage(),
        QStringLiteral("Could not edit the tags: the library could not be read"));
}

void ToolsShellReadFailTest::undoSaysTheLibraryWasNotRead()
{
    // L-READFAIL 3, UN-*: the undo puts the file back; the library cannot then be read, so it is
    // not brought up to date. The message says both: what was put back, and that the library shows
    // the old tags until it can be read.
    Rig r;
    if (!r.tools->tagUndo())
        QFAIL("the shell has no undo");
    QVERIFY(r.lib->navigator()->selectNode(QStringLiteral("library:Music")));
    const QString before = readTags(r.a).tags.first(QStringLiteral("Album"));
    r.select({ r.a });
    r.work = [](QDialog *dialog) {
        auto *editor = qobject_cast<TagEditorDialog *>(dialog);
        if (!editor)
            QFAIL("not the tag editor");
        type(editor, "field.Album", QStringLiteral("Changed Album"));
        QVERIFY(editor->save());
    };
    r.run("file.editTags");
    QCOMPARE(readTags(r.a).tags.first(QStringLiteral("Album")), QStringLiteral("Changed Album"));
    QVERIFY(damageOpen(r.file));
    r.run("edit.undo");
    QCOMPARE(readTags(r.a).tags.first(QStringLiteral("Album")), before);
    QCOMPARE(r.tools->lastMessage(),
        QStringLiteral("Undid Edit Tags: 1 file put back; the library could not be read, so it "
                       "shows the old tags until it can"));
}

QTEST_MAIN(ToolsShellReadFailTest)
#include "tst_ui_toolsshell_readfail.moc"
