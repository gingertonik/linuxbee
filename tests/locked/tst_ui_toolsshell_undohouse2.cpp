// LOCKED ACCEPTANCE TEST (the undo's run folder: no folder means no picture kept; a linked root is refused; one start at a time).
// Written by the reviewer, not the builder. The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/tools-shell.md (TS-UNDO, "The run folder") and docs/spec/tag-undo.md (UN-NOKEEP).
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
#include <QDirIterator>
#include <QElapsedTimer>
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

// Every file under a folder, as paths from it; links are listed and not entered.
QStringList everything(const QString &folder)
{
    QStringList out;
    QDirIterator it(folder, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        out.append(QDir(folder).relativeFilePath(it.filePath()));
    }
    out.sort();
    return out;
}

// What a save and an undo of a plain tag look like when all is well.
bool albumEditAndBack(Rig &r)
{
    TagUndo *undo = r.tools->tagUndo();
    const QString before = readTags(r.a).tags.first(s(u"Album"));
    TagChanges c;
    c.fields = { { s(u"Album"), { s(u"Changed") } } };
    undo->begin(s(u"Test"));
    const bool wrote = undo->write(r.a, c).ok();
    undo->end();
    return wrote && undo->canUndo() && undo->undo().failed.isEmpty() && readTags(r.a).tags.first(s(u"Album")) == before;
}

// A picture removal is refused and leaves the picture where it is.
bool pictureRemovalRefused(Rig &r)
{
    TagUndo *undo = r.tools->tagUndo();
    undo->begin(s(u"Test"));
    const bool wrote = undo->write(r.b, noPictures()).ok();
    undo->end();
    return !wrote && pictureCount(r.b) == 1;
}

}

class ToolsShellUndoHouse2Test : public QObject {
    Q_OBJECT
private slots:
    void noFolderMeansNoPictureIsEverKept();
    void aRootThatIsALinkIsRefused();
    void oneStartAtATime();
};

void ToolsShellUndoHouse2Test::noFolderMeansNoPictureIsEverKept()
{
    // TS-UNDO: the shell started with no run folder. Later the place becomes usable (the review's case: a full
    // disk that is then freed). Still no picture is kept anywhere, for as long as this shell lives.
    (void)&sorted;
    (void)&album;
    (void)&idOf;
    (void)&updated;
    (void)&editAlbum;
    QTemporaryDir outer;
    QTemporaryDir here; // the current folder, so that nothing can land in a folder of the user's
    const QString was = QDir::currentPath();
    QVERIFY(QDir::setCurrent(here.path()));
    const auto back = qScopeGuard([was] { QDir::setCurrent(was); });
    write(outer.path() + QStringLiteral("/blocked"), "x");
    Cache cache(outer.path() + QStringLiteral("/blocked/cache")); // nothing can be made under a file
    Rig r;
    NEED_UNDO(r);
    QVERIFY(pictureRemovalRefused(r));
    // Now the place can be used.
    QVERIFY(QFile::remove(outer.path() + QStringLiteral("/blocked")));
    QVERIFY(QDir().mkpath(outer.path() + QStringLiteral("/blocked/cache")));
    QVERIFY(pictureRemovalRefused(r));
    QVERIFY(albumEditAndBack(r));
    QCOMPARE(everything(outer.path()), QStringList({ s(u"blocked"), s(u"blocked/cache") })); // nothing was made in it
    QCOMPARE(everything(here.path()), QStringList()); // and nothing in the current folder
    QCOMPARE(listing(r.dir.path() + QStringLiteral("/music")).size(), 3);
}

void ToolsShellUndoHouse2Test::aRootThatIsALinkIsRefused()
{
    // TS-UNDO: `undo` itself must be a real folder. A link there is not followed: nothing is tidied through it,
    // no run folder is made through it, and the shell has no folder.
    Cache cache;
    const QString victim = cache.dir.path() + QStringLiteral("/victim");
    write(victim + QStringLiteral("/run-planted/pictures/one.picture"), "not ours"); // no lock: it would look dead
    write(victim + QStringLiteral("/run-pfile"), "not ours");
    write(victim + QStringLiteral("/other.txt"), "not ours");
    const QStringList before = everything(victim);
    QVERIFY(QDir().mkpath(cache.dir.path() + QStringLiteral("/cache/linuxbee")));
    QVERIFY(QFile::link(victim, cache.root()));
    {
        Rig r;
        NEED_UNDO(r);
        QCOMPARE(everything(victim), before);
        QVERIFY(pictureRemovalRefused(r));
        QVERIFY(albumEditAndBack(r));
        QCOMPARE(everything(victim), before);
    }
    QCOMPARE(everything(victim), before);
    QVERIFY(QFileInfo(cache.root()).isSymLink()); // the link itself is left for the user to see
    // A folder above the root may be a link (a cache kept on another disk): that works as usual.
    Cache other;
    const QString elsewhere = other.dir.path() + QStringLiteral("/elsewhere");
    QVERIFY(QDir().mkpath(elsewhere));
    QVERIFY(QDir().mkpath(other.dir.path() + QStringLiteral("/cache")));
    QVERIFY(QFile::link(elsewhere, other.dir.path() + QStringLiteral("/cache/linuxbee")));
    Rig r;
    QCOMPARE(other.runs().size(), 1);
    QVERIFY(!pictureRemovalRefused(r)); // it has a folder, so the picture is kept and removed
    QCOMPARE(pictureCount(r.b), 0);
}

void ToolsShellUndoHouse2Test::oneStartAtATime()
{
    // TS-UNDO: tidying and making the run folder happen under `start.lock` in the root, so that one program's
    // tidying cannot take the folder another has just made. A start that cannot have the lock within two
    // seconds tidies nothing and has no folder.
    Cache cache;
    const QString root = cache.root();
    write(root + QStringLiteral("/run-dead01/pictures/one.picture"), "cover");
    QLockFile starting(root + QStringLiteral("/start.lock"));
    starting.setStaleLockTime(0);
    QVERIFY(starting.tryLock(0));
    QElapsedTimer timer;
    timer.start();
    {
        Rig r;
        NEED_UNDO(r);
        const qint64 waited = timer.elapsed();
        QVERIFY2(waited >= 1500 && waited < 15000, qPrintable(QString::number(waited)));
        QCOMPARE(cache.runs(), QStringList({ s(u"run-dead01") })); // not tidied, and no folder of its own
        QVERIFY(QFileInfo::exists(root + QStringLiteral("/run-dead01/pictures/one.picture")));
        QVERIFY(pictureRemovalRefused(r));
        QVERIFY(albumEditAndBack(r));
    }
    starting.unlock();
    // An ordinary start: it tidies, has its folder, and lets the start lock go as soon as it has started.
    Rig r;
    QVERIFY(!QFileInfo::exists(root + QStringLiteral("/run-dead01")));
    QCOMPARE(cache.runs().size(), 1);
    QLockFile again(root + QStringLiteral("/start.lock"));
    again.setStaleLockTime(0);
    QVERIFY(again.tryLock(0));
    again.unlock();
    // The start lock is not a run folder and is never tidied as one.
    Rig next;
    QCOMPARE(cache.runs().size(), 2);
}

QTEST_MAIN(ToolsShellUndoHouse2Test)
#include "tst_ui_toolsshell_undohouse2.moc"
