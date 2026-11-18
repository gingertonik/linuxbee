// LOCKED ACCEPTANCE TEST (the tools shell: a file that undo could not put back can be tried
// again). Written by the reviewer, not the builder. The builder makes these pass by changing src/,
// never by editing this file. A test believed to be wrong is reported, with the reason, and left
// alone.
//
// Expected values: TS-UNDO in docs/spec/tools-shell.md, UN-KEEP in docs/spec/tag-undo.md.
// Offscreen, with the real window, library and shell, in a temporary folder. The rig is the one
// of tst_ui_toolsshell_undo.cpp.
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPointer>
#include <QSettings>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTemporaryDir>
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
        lib = std::make_unique<LibraryShell>(
            &window, &actions, library.get(), &playlists, &settings, ShellHooks { });
        tools = std::make_unique<ToolsShell>(
            &window, &actions, library.get(), lib.get(), &settings, [this](QDialog *dialog) {
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
        if (!QFile::copy(
                QStringLiteral(LINUXBEE_TEST_DATA) + QLatin1Char('/') + QLatin1String(from),
                target))
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

QString albumOf(const QString &path)
{
    return readTags(path).tags.first(QStringLiteral("Album"));
}

// One step through the shell's own undo, as a dialog's saver makes it: the album of each file.
void editAlbums(Rig &r, const QStringList &paths)
{
    TagUndo *undo = r.tools->tagUndo();
    undo->begin(s(u"Edit Tags"));
    for (const QString &path : paths) {
        TagChanges c;
        c.fields.insert(QStringLiteral("Album"), { s(u"Changed") });
        if (undo->write(path, c).status != WriteResult::Status::Ok)
            QTest::qFail("the edit was not saved", __FILE__, __LINE__);
        if (const auto t = r.library->trackByPath(path))
            r.library->refresh(t->id);
    }
    undo->end();
}

}

class ToolsShellUndoKeepTest : public QObject {
    Q_OBJECT
private slots:
    void aFileThatCouldNotBeWrittenIsTriedAgain();
    void twoThatCouldNot();
};

void ToolsShellUndoKeepTest::aFileThatCouldNotBeWrittenIsTriedAgain()
{
    Rig r;
    const QString beforeA = albumOf(r.a);
    const QString beforeB = albumOf(r.b);
    editAlbums(r, { r.a, r.b });
    QVERIFY(QFile::setPermissions(r.b, QFile::ReadOwner)); // marked read-only meanwhile
    r.run(u"edit.undo");
    // TS-UNDO 3: the message says that the file can be tried again.
    QCOMPARE(r.tools->lastMessage(),
        s(u"Undid Edit Tags: 1 file put back; 1 could not be put back (Undo tries again)"));
    QCOMPARE(albumOf(r.a), beforeA);
    QCOMPARE(albumOf(r.b), s(u"Changed"));
    QVERIFY(r.tools->tagUndo()->canUndo());

    // Still not writable: said again, and still kept.
    r.run(u"edit.undo");
    QCOMPARE(r.tools->lastMessage(),
        s(u"Undid Edit Tags: 0 files put back; 1 could not be put back (Undo tries again)"));

    QVERIFY(QFile::setPermissions(r.b, QFile::ReadOwner | QFile::WriteOwner));
    r.run(u"edit.undo");
    QCOMPARE(r.tools->lastMessage(), s(u"Undid Edit Tags: 1 file put back"));
    QCOMPARE(albumOf(r.b), beforeB);
    // TS-UNDO 2: the library has read the file again.
    const auto track = r.library->trackByPath(r.b);
    QVERIFY(track.has_value());
    QCOMPARE(track->tags.first(QStringLiteral("Album")), beforeB);

    r.run(u"edit.undo");
    QCOMPARE(r.tools->lastMessage(), s(u"Nothing to undo"));
}

void ToolsShellUndoKeepTest::twoThatCouldNot()
{
    Rig r;
    editAlbums(r, { r.a, r.b });
    QVERIFY(QFile::setPermissions(r.a, QFile::ReadOwner));
    QVERIFY(QFile::setPermissions(r.b, QFile::ReadOwner));
    r.run(u"edit.undo");
    QCOMPARE(r.tools->lastMessage(),
        s(u"Undid Edit Tags: 0 files put back; 2 could not be put back (Undo tries again)"));
    QCOMPARE(r.tools->tagUndo()->paths(), QStringList({ r.a, r.b }));
}

QTEST_MAIN(ToolsShellUndoKeepTest)
#include "tst_ui_toolsshell_undo_keep.moc"
