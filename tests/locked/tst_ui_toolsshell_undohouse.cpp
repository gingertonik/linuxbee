// LOCKED ACCEPTANCE TEST (the tools shell: where the undo keeps its pictures, and tidying up after a run that died). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/tools-shell.md (TS-UNDO, "The run folder").
// Every test names a cache folder of its own before a shell is made; nothing of the user's is looked at.
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPointer>
#include <QSettings>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QLockFile>
#include <QStandardPaths>
#include <QtTest>

#include "actions/actionregistry.h"
#include "convert/convertdialog.h"
#include "mainwindow/mainwindow.h"
#include "organise/organisedialog.h"
#include "preferences/hotkeyspage.h"
#include "preferences/preferencesdialog.h"
#include "shell/libraryshell.h"
#include "shell/toolsshell.h"
#include "tageditor/tageditordialog.h"
#include "tagtools/capitalisepanel.h"
#include "tagtools/searchreplacepanel.h"
#include "tagtools/removetagspanel.h"
#include "tagtools/tagtooldialog.h"
#include "tagundo/tagundo.h"
#include "tracklist/tracklistpage.h"

using namespace linuxbee;
using namespace linuxbee::ui;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

QStringList sorted(QStringList list)
{
    list.sort();
    return list;
}

struct Rig {
    QTemporaryDir dir;
    QString a, b, tone;
    std::unique_ptr<Library> library;
    PlaylistStore playlists;
    QSettings settings;
    ActionRegistry actions;
    MainWindow window;
    QDateTime now = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);
    std::unique_ptr<LibraryShell> lib;
    std::unique_ptr<ToolsShell> tools;
    int presented = 0;
    QString kind; // class name of the dialog last presented
    QPointer<QDialog> last;
    std::function<void(QDialog *)> work; // what "the user" does with the dialog

    Rig()
        : library(Library::open(QStringLiteral(":memory:")))
        , playlists(made(QStringLiteral("playlists")))
        , settings(dir.path() + QStringLiteral("/s.ini"), QSettings::IniFormat)
        , window(&actions, &settings)
    {
        made(QStringLiteral("music"));
        a = put("tags/basic.flac", "music/a.flac");
        b = put("tags/basic.m4a", "music/b.m4a");
        tone = put("convert/tone.flac", "music/tone.flac");
        for (const QString &p : { a, b, tone })
            if (library->addFile(p, QStringLiteral("Music"), now) == 0)
                QTest::qFail("a fixture did not go into the library", __FILE__, __LINE__);
        lib = std::make_unique<LibraryShell>(&window, &actions, library.get(), &playlists, &settings, ShellHooks { });
        tools = std::make_unique<ToolsShell>(&window, &actions, library.get(), lib.get(), &settings, [this](QDialog *dialog) {
            ++presented;
            last = dialog;
            kind = QString::fromLatin1(dialog->metaObject()->className());
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
        if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA) + QLatin1Char('/') + QLatin1String(from), target))
            QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
        return target;
    }
    void run(const char16_t *id)
    {
        if (QAction *action = actions.action(s(id)))
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
    QString title(const QString &path) const
    {
        const auto t = library->trackByPath(path);
        return t ? t->tags.first(QStringLiteral("Title")) : QStringLiteral("NOT IN THE LIBRARY");
    }
};

void type(QDialog *dialog, const char16_t *name, const QString &text)
{
    if (auto *edit = dialog->findChild<QLineEdit *>(s(name))) {
        // Keys, as a person types: a line edit knows it was modified only then.
        edit->setFocus();
        edit->selectAll();
        if (text.isEmpty())
            QTest::keyClick(edit, Qt::Key_Delete);
        else
            QTest::keyClicks(edit, text);
    } else {
        QTest::qFail(qPrintable(QStringLiteral("no line edit named ") + s(name)), __FILE__, __LINE__);
    }
}

qint64 idOf(const Rig &r, const QString &path)
{
    const auto t = r.library->trackByPath(path);
    return t ? t->id : 0;
}

// Every id the library reported as updated, in order.
QList<qint64> updated(const QSignalSpy &spy)
{
    QList<qint64> ids;
    for (const QList<QVariant> &call : spy)
        ids += call.at(0).value<QList<qint64>>();
    return ids;
}

#define NEED_UNDO(r) \
    do { \
        if (!(r).tools->tagUndo()) \
            QFAIL("the shell has no undo"); \
    } while (false)

// Changes the album of the selected tracks in the tag editor.
void editAlbum(Rig &r, const QString &album)
{
    r.work = [&r, album](QDialog *dialog) {
        auto *editor = qobject_cast<TagEditorDialog *>(dialog);
        if (!editor)
            QFAIL("not the tag editor");
        type(editor, u"field.Album", album);
        QVERIFY(editor->save());
    };
    r.run(u"file.editTags");
}

QString album(const Rig &r, const QString &path)
{
    const auto t = r.library->trackByPath(path);
    return t ? t->tags.first(QStringLiteral("Album")) : QStringLiteral("NOT IN THE LIBRARY");
}

// A cache folder of this test's own, named before the shell is made.
struct Cache {
    QTemporaryDir dir;
    explicit Cache(const QString &under = QString())
    {
        qputenv("XDG_CACHE_HOME", QFile::encodeName(under.isEmpty() ? dir.path() + QStringLiteral("/cache") : under));
    }
    QString root() const { return dir.path() + QStringLiteral("/cache/linuxbee/undo"); }
    QStringList runs() const
    {
        return QDir(root()).entryList({ QStringLiteral("run-*") }, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System, QDir::Name);
    }
};

void write(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        QTest::qFail("could not write a file", __FILE__, __LINE__);
}

QStringList listing(const QString &folder)
{
    return QDir(folder).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
}

int pictureCount(const QString &path)
{
    return int(readTags(path).tags.pictures.size());
}

TagChanges noPictures()
{
    TagChanges c;
    c.pictures = QList<Picture> { };
    return c;
}

}

class ToolsShellUndoHouseTest : public QObject {
    Q_OBJECT
private slots:
    void aFolderOfItsOwn();
    void whatADeadRunLeftGoes();
    void whenTheFolderCannotBeMade();
    void theMusicFolderGainsNothing();
    void notWhileAChangeIsOpen();
};

void ToolsShellUndoHouseTest::aFolderOfItsOwn()
{
    // TS-UNDO, the run folder: one folder under the cache folder, the user's alone, locked while the shell lives.
    (void)&sorted;
    (void)&album;
    (void)&idOf;
    (void)&updated;
    Cache cache;
    QCOMPARE(QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation), cache.dir.path() + QStringLiteral("/cache"));
    {
        Rig r;
        NEED_UNDO(r);
        const QStringList runs = cache.runs();
        QCOMPARE(runs.size(), 1);
        const QString run = cache.root() + QLatin1Char('/') + runs.first();
        QVERIFY(QFileInfo(run).isDir() && !QFileInfo(run).isSymLink());
        QCOMPARE(QFile::permissions(run)
                & (QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup | QFile::ReadOther | QFile::WriteOther | QFile::ExeOther),
            QFile::Permissions());
        QVERIFY(QFileInfo::exists(run + QStringLiteral("/lock")));
        // Held: nobody else can take it.
        QLockFile other(run + QStringLiteral("/lock"));
        other.setStaleLockTime(0);
        QVERIFY(!other.tryLock(0));
        // A removed picture is kept inside it, in "pictures".
        QCOMPARE(pictureCount(r.b), 1); // GEN: basic.m4a has one
        r.tools->tagUndo()->begin(s(u"Test"));
        QVERIFY(r.tools->tagUndo()->write(r.b, noPictures()).ok());
        r.tools->tagUndo()->end();
        QCOMPARE(pictureCount(r.b), 0);
        QCOMPARE(listing(run + QStringLiteral("/pictures")).size(), 1);
        // A second shell beside it has a folder of its own and leaves the first alone.
        Rig second;
        QCOMPARE(cache.runs().size(), 2);
        QCOMPARE(listing(run + QStringLiteral("/pictures")).size(), 1);
        QVERIFY(r.tools->tagUndo()->undo().failed.isEmpty());
        QCOMPARE(pictureCount(r.b), 1);
    }
    // Both shells are gone: so are their folders.
    QCOMPARE(cache.runs(), QStringList());
}

void ToolsShellUndoHouseTest::whatADeadRunLeftGoes()
{
    // TS-UNDO, the run folder: at start, the folders of runs that are no longer alive are removed; nothing else is.
    Cache cache;
    const QString root = cache.root();
    write(root + QStringLiteral("/run-dead01/pictures/one.picture"), "cover"); // no lock file at all
    write(root + QStringLiteral("/run-dead02/pictures/two.picture"), "cover");
    {
        // A lock that was taken and let go, as a run that ended leaves it.
        QLockFile was(root + QStringLiteral("/run-dead02/lock"));
        QVERIFY(was.tryLock(0));
        was.unlock();
    }
    QDir().mkpath(root + QStringLiteral("/run-empty3"));
    // Not a run folder by its name: not touched.
    write(root + QStringLiteral("/keep-me/pictures/three.picture"), "mine");
    write(root + QStringLiteral("/run.txt"), "a file");
    // A run that is alive: its lock is held, and was taken an hour ago.
    write(root + QStringLiteral("/run-alive4/pictures/four.picture"), "in use");
    QLockFile alive(root + QStringLiteral("/run-alive4/lock"));
    alive.setStaleLockTime(0);
    QVERIFY(alive.tryLock(0));
    {
        QFile lock(root + QStringLiteral("/run-alive4/lock"));
        QVERIFY(lock.open(QIODevice::ReadWrite));
        QVERIFY(lock.setFileTime(QDateTime::currentDateTimeUtc().addSecs(-3600), QFileDevice::FileModificationTime));
    }
    // A link with a run folder's name: the link goes, what it leads to is not entered.
    write(cache.dir.path() + QStringLiteral("/elsewhere/pictures/five.picture"), "not ours");
    QVERIFY(QFile::link(cache.dir.path() + QStringLiteral("/elsewhere"), root + QStringLiteral("/run-link05")));
    // A plain file with a run folder's name goes too.
    write(root + QStringLiteral("/run-file06"), "x");

    Rig r;
    NEED_UNDO(r);
    const QStringList runs = cache.runs();
    QVERIFY2(runs.contains(QStringLiteral("run-alive4")), qPrintable(runs.join(QLatin1Char(' '))));
    QVERIFY2(runs.size() == 2, qPrintable(runs.join(QLatin1Char(' ')))); // the live one, and this shell's own
    QVERIFY(QFileInfo::exists(root + QStringLiteral("/run-alive4/pictures/four.picture")));
    QVERIFY(QFileInfo::exists(root + QStringLiteral("/keep-me/pictures/three.picture")));
    QVERIFY(QFileInfo::exists(root + QStringLiteral("/run.txt")));
    QVERIFY(QFileInfo::exists(cache.dir.path() + QStringLiteral("/elsewhere/pictures/five.picture")));
    QVERIFY(!QFileInfo(root + QStringLiteral("/run-link05")).isSymLink());
    // The live run ends: its folder is tidied by the next shell to start, not before.
    alive.unlock();
    QVERIFY(QFileInfo::exists(root + QStringLiteral("/run-alive4/pictures/four.picture")));
    Rig next;
    QVERIFY(!QFileInfo::exists(root + QStringLiteral("/run-alive4")));
    QCOMPARE(cache.runs().size(), 2); // r's and next's
}

void ToolsShellUndoHouseTest::whenTheFolderCannotBeMade()
{
    // TS-UNDO, the run folder: with no folder, tags are still saved and undone; a picture removal is refused,
    // and nothing is written anywhere else.
    QTemporaryDir outer;
    write(outer.path() + QStringLiteral("/a-file"), "x");
    Cache cache(outer.path() + QStringLiteral("/a-file/cache")); // a folder cannot be made under a file
    const bool rootHadPictures = QFileInfo::exists(QStringLiteral("/pictures"));
    // A temporary folder of this test's own, so that its listing below is this test's alone.
    QTemporaryDir ownTemp;
    const QByteArray tempWas = qgetenv("TMPDIR");
    qputenv("TMPDIR", QFile::encodeName(ownTemp.path()));
    const auto restore = qScopeGuard([&tempWas] { tempWas.isEmpty() ? qunsetenv("TMPDIR") : qputenv("TMPDIR", tempWas); });
    QCOMPARE(QDir::tempPath(), ownTemp.path());
    Rig r;
    NEED_UNDO(r);
    const QStringList tempBefore = listing(QDir::tempPath());
    TagUndo *undo = r.tools->tagUndo();
    const QString before = readTags(r.a).tags.first(s(u"Album"));
    TagChanges c;
    c.fields = { { s(u"Album"), { s(u"Changed") } } };
    undo->begin(s(u"Test"));
    QVERIFY(undo->write(r.a, c).ok());
    QVERIFY(!undo->write(r.b, noPictures()).ok()); // the picture could not be kept, so it is not removed
    undo->end();
    QCOMPARE(pictureCount(r.b), 1);
    QCOMPARE(readTags(r.a).tags.first(s(u"Album")), s(u"Changed"));
    QVERIFY(undo->canUndo());
    QVERIFY(undo->undo().failed.isEmpty());
    QCOMPARE(readTags(r.a).tags.first(s(u"Album")), before);
    QCOMPARE(QFileInfo::exists(QStringLiteral("/pictures")), rootHadPictures);
    QCOMPARE(listing(QDir::tempPath()), tempBefore); // and no picture went into the temporary folder instead
    QCOMPARE(listing(r.dir.path() + QStringLiteral("/music")).size(), 3);
}

void ToolsShellUndoHouseTest::theMusicFolderGainsNothing()
{
    // TS-UNDO: a kept picture is never put beside the music.
    Cache cache;
    Rig r;
    NEED_UNDO(r);
    const QString music = r.dir.path() + QStringLiteral("/music");
    const QStringList before = listing(music);
    QCOMPARE(before.size(), 3);
    r.tools->tagUndo()->begin(s(u"Test"));
    QVERIFY(r.tools->tagUndo()->write(r.b, noPictures()).ok());
    QCOMPARE(listing(music), before);
    r.tools->tagUndo()->end();
    QCOMPARE(listing(music), before);
    r.run(u"edit.undo");
    QCOMPARE(pictureCount(r.b), 1);
    QCOMPARE(listing(music), before);
}

void ToolsShellUndoHouseTest::notWhileAChangeIsOpen()
{
    // TS-UNDO: with a dialog's change open, Undo says so and does nothing; the earlier step is still there after.
    Cache cache;
    Rig r;
    NEED_UNDO(r);
    const QString albumBefore = readTags(r.a).tags.first(s(u"Album"));
    r.select({ r.a });
    editAlbum(r, s(u"A New Album"));
    QVERIFY(r.tools->tagUndo()->canUndo());
    QString said;
    r.work = [&](QDialog *) {
        r.run(u"edit.undo");
        said = r.tools->lastMessage();
    };
    r.run(u"file.editTags");
    QCOMPARE(said, s(u"Undo is not available while a change is being made"));
    QCOMPARE(readTags(r.a).tags.first(s(u"Album")), s(u"A New Album")); // nothing was undone
    QVERIFY(r.tools->tagUndo()->canUndo()); // the dialog changed nothing, so the step before it stands
    r.work = nullptr;
    r.run(u"edit.undo");
    QCOMPARE(readTags(r.a).tags.first(s(u"Album")), albumBefore);
    // With no step at all and nothing open, the old message.
    r.run(u"edit.undo");
    QCOMPARE(r.tools->lastMessage(), s(u"Nothing to undo"));
}

QTEST_MAIN(ToolsShellUndoHouseTest)
#include "tst_ui_toolsshell_undohouse.moc"
