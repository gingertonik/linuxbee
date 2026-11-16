// LOCKED ACCEPTANCE TEST (Edit > Undo takes the last tag change back). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/tools-shell.md (TS-UNDO) and
// docs/spec/tag-undo.md. Offscreen, with the real window, library and
// shells; a function is handed each dialog and works it.
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

}

class ToolsShellUndoTest : public QObject {
    Q_OBJECT
private slots:
    void theEditorAndBack();
    void aToolAndBack();
    void nothingToUndo();
    void picturesComeBack();
    void aFileThatWentAway();
};

void ToolsShellUndoTest::theEditorAndBack()
{
    // TS-UNDO: a change made in the tag editor, and Edit > Undo: the files and the library are as before.
    (void)&sorted;
    Rig r;
    NEED_UNDO(r);
    QVERIFY(r.actions.isImplemented(s(u"edit.undo")));
    const QString albumA = album(r, r.a);
    const QString albumB = album(r, r.b);
    r.select({ r.a, r.b });
    editAlbum(r, s(u"A New Album"));
    QCOMPARE(readTags(r.a).tags.first(s(u"Album")), s(u"A New Album"));
    QVERIFY(r.tools->tagUndo()->canUndo());
    QCOMPARE(r.tools->tagUndo()->name(), s(u"Edit Tags"));
    QCOMPARE(sorted(r.tools->tagUndo()->paths()), sorted({ r.a, r.b }));

    QSignalSpy changed(r.library.get(), &Library::tracksUpdated);
    r.presented = 0;
    r.run(u"edit.undo");
    QCOMPARE(r.presented, 0); // it asks nothing and opens nothing
    QCOMPARE(readTags(r.a).tags.first(s(u"Album")), albumA);
    QCOMPARE(readTags(r.b).tags.first(s(u"Album")), albumB);
    QCOMPARE(album(r, r.a), albumA); // the library has read them again
    QCOMPARE(album(r, r.b), albumB);
    QList<qint64> ids = updated(changed);
    std::sort(ids.begin(), ids.end());
    QList<qint64> want { idOf(r, r.a), idOf(r, r.b) };
    std::sort(want.begin(), want.end());
    QCOMPARE(ids, want); // each once, and no other track
    QCOMPARE(r.tools->lastMessage(), s(u"Undid Edit Tags: 2 files put back"));
    QCOMPARE(r.window.statusBar()->currentMessage(), r.tools->lastMessage());
    QVERIFY(!r.tools->tagUndo()->canUndo());
    QCOMPARE(r.title(r.a), s(u"Flac Title")); // what was not changed was not touched
}

void ToolsShellUndoTest::aToolAndBack()
{
    // TS-UNDO: the same for a tagging tool; the step has the tool's name, and only the files written are in it.
    Rig r;
    NEED_UNDO(r);
    r.select({ r.a, r.b });
    r.work = [&](QDialog *dialog) {
        auto *tool = qobject_cast<TagToolDialog *>(dialog);
        QVERIFY(tool);
        type(tool, u"findEdit", s(u"Flac Title"));
        type(tool, u"replaceEdit", s(u"Lossless"));
        QVERIFY(tool->apply());
    };
    r.run(u"tools.tagging.searchReplace");
    QCOMPARE(r.title(r.a), s(u"Lossless"));
    QCOMPARE(r.tools->tagUndo()->name(), s(u"Search and Replace"));
    QCOMPARE(r.tools->tagUndo()->paths(), QStringList({ r.a }));
    r.run(u"edit.undo");
    QCOMPARE(r.title(r.a), s(u"Flac Title"));
    QCOMPARE(readTags(r.a).tags.first(s(u"Title")), s(u"Flac Title"));
    QCOMPARE(r.tools->lastMessage(), s(u"Undid Search and Replace: 1 file put back"));
}

void ToolsShellUndoTest::nothingToUndo()
{
    // TS-UNDO: with no step the command says so. A dialog that was closed without a change keeps the step before it.
    Rig r;
    NEED_UNDO(r);
    r.run(u"edit.undo");
    QCOMPARE(r.tools->lastMessage(), s(u"Nothing to undo"));
    const QString before = album(r, r.a);
    r.select({ r.a });
    editAlbum(r, s(u"Changed"));
    // Opened and closed: the editor, then a tool.
    r.work = [](QDialog *dialog) {
        type(dialog, u"field.Title", QStringLiteral("Never saved"));
        dialog->reject();
    };
    r.run(u"file.editTags");
    r.work = [](QDialog *dialog) { dialog->reject(); };
    r.run(u"tools.tagging.capitalise");
    QCOMPARE(r.tools->tagUndo()->name(), s(u"Edit Tags"));
    r.run(u"edit.undo");
    QCOMPARE(album(r, r.a), before);
    QCOMPARE(r.tools->lastMessage(), s(u"Undid Edit Tags: 1 file put back"));
    // Once: a second undo has nothing.
    r.run(u"edit.undo");
    QCOMPARE(r.tools->lastMessage(), s(u"Nothing to undo"));
    QCOMPARE(album(r, r.a), before);
    // The next change is the step now, not the one before it.
    editAlbum(r, s(u"First"));
    editAlbum(r, s(u"Second"));
    r.run(u"edit.undo");
    QCOMPARE(album(r, r.a), s(u"First"));
}

void ToolsShellUndoTest::picturesComeBack()
{
    // TS-UNDO: embedded pictures removed by Remove Tags come back with undo.
    Rig r;
    NEED_UNDO(r);
    Picture cover;
    cover.data = QByteArray::fromHex(
        "89504e470d0a1a0a0000000d4948445200000001000000010802000000907753de"
        "0000000c49444154789c63f8cfc0000003010100c9fe92ef0000000049454e44ae426082");
    cover.mimeType = s(u"image/png");
    TagChanges give;
    give.pictures = QList<Picture> { cover };
    QVERIFY(writeTags(r.a, give).ok());
    r.library->refresh(idOf(r, r.a));
    r.select({ r.a });
    QString asked;
    r.work = [&](QDialog *dialog) {
        auto *tool = qobject_cast<TagToolDialog *>(dialog);
        QVERIFY(tool);
        tool->setConfirm([&asked](const QString &question) {
            asked = question;
            return true;
        });
        auto *pictures = tool->findChild<QCheckBox *>(s(u"picturesBox"));
        QVERIFY(pictures);
        pictures->setChecked(true);
        QVERIFY(tool->apply());
    };
    r.run(u"tools.tagging.removeTags");
    QCOMPARE(readTags(r.a).tags.pictures.size(), 0);
    // TT-WARN: the question no longer says it cannot be undone.
    QCOMPARE(asked, s(u"Remove the embedded pictures from 1 file? Undo brings them back, until the next tag change or until the program closes."));
    r.run(u"edit.undo");
    const TrackTags after = readTags(r.a).tags;
    QCOMPARE(after.pictures.size(), 1);
    QCOMPARE(after.pictures.at(0).data, cover.data);
    QCOMPARE(r.tools->lastMessage(), s(u"Undid Remove Tags: 1 file put back"));
}

void ToolsShellUndoTest::aFileThatWentAway()
{
    // TS-UNDO: a file that cannot be written is counted in the message; the others are put back.
    Rig r;
    NEED_UNDO(r);
    const QString before = album(r, r.a);
    r.select({ r.a, r.b });
    editAlbum(r, s(u"Changed"));
    QVERIFY(QFile::remove(r.b));
    r.run(u"edit.undo");
    QCOMPARE(readTags(r.a).tags.first(s(u"Album")), before);
    QCOMPARE(album(r, r.a), before);
    QCOMPARE(r.tools->lastMessage(), s(u"Undid Edit Tags: 1 file put back; 1 could not be put back"));
}

QTEST_MAIN(ToolsShellUndoTest)
#include "tst_ui_toolsshell_undo.moc"
