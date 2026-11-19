// LOCKED ACCEPTANCE TEST (the tools shell: undo follows a file the library moved, and leaves a
// file that is not the one edited). Written by the reviewer, not the builder. The builder makes
// these pass by changing src/, never by editing this file. A test believed to be wrong is
// reported, with the reason, and left alone.
//
// Expected values: TS-UNDO in docs/spec/tools-shell.md, UN-SAME and UN-MOVED in
// docs/spec/tag-undo.md. Offscreen, with the real window, library and dialogs, in a temporary
// folder. The rig is the one of tst_ui_toolsshell_undo.cpp.
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
        QTest::qFail(
            qPrintable(QStringLiteral("no line edit named ") + s(name)), __FILE__, __LINE__);
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

#define NEED_UNDO(r)                                                                               \
    do {                                                                                           \
        if (!(r).tools->tagUndo())                                                                 \
            QFAIL("the shell has no undo");                                                        \
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

class ToolsShellUndoMovedTest : public QObject {
    Q_OBJECT
private slots:
    void theLibraryMovedTheFile();
    void anotherFileHasTheOldName();
    void aFileThatIsNotTheOneEdited();
};

void ToolsShellUndoMovedTest::theLibraryMovedTheFile()
{
    // TS-UNDO: the shell tells the undo step of every move the library reports (L-MOVED). A
    // change, then the file renamed and the library told, then Edit > Undo: the tags are back, in
    // the file where it is now.
    (void)&sorted;
    (void)&updated;
    Rig r;
    NEED_UNDO(r);
    const QString before = album(r, r.a);
    const qint64 id = idOf(r, r.a);
    r.select({ r.a });
    editAlbum(r, s(u"Changed"));
    const QString to = r.dir.path() + s(u"/music/renamed.flac");
    QVERIFY(QFile::rename(r.a, to));
    QVERIFY(r.library->setPath(id, to));
    QCOMPARE(r.tools->tagUndo()->paths(), QStringList({ to }));
    r.run(u"edit.undo");
    QCOMPARE(readTags(to).tags.first(s(u"Album")), before);
    QCOMPARE(album(r, to), before); // and the library has read it again
    QCOMPARE(r.tools->lastMessage(), s(u"Undid Edit Tags: 1 file put back"));
}

void ToolsShellUndoMovedTest::anotherFileHasTheOldName()
{
    // The audit's case: after the move, another file is given the old name. It is not written.
    Rig r;
    NEED_UNDO(r);
    const QString before = album(r, r.a);
    const qint64 id = idOf(r, r.a);
    r.select({ r.a });
    editAlbum(r, s(u"Changed"));
    const QString to = r.dir.path() + s(u"/music/renamed.flac");
    QVERIFY(QFile::rename(r.a, to));
    QVERIFY(r.library->setPath(id, to));
    QVERIFY(QFile::copy(r.tone, r.a));
    QFile stranger(r.a);
    QVERIFY(stranger.open(QIODevice::ReadOnly));
    const QByteArray was = stranger.readAll();
    stranger.close();
    r.run(u"edit.undo");
    QCOMPARE(readTags(to).tags.first(s(u"Album")), before);
    QVERIFY(stranger.open(QIODevice::ReadOnly));
    QCOMPARE(stranger.readAll(), was);
    QCOMPARE(r.tools->lastMessage(), s(u"Undid Edit Tags: 1 file put back"));
}

void ToolsShellUndoMovedTest::aFileThatIsNotTheOneEdited()
{
    // UN-SAME through the window: the edited file was taken away behind the program's back and
    // another put in its place. Undo leaves it, and the message says so and does not say "put back"
    // of it. The file that was not touched is put back.
    Rig r;
    NEED_UNDO(r);
    const QString beforeB = album(r, r.b);
    r.select({ r.a, r.b });
    editAlbum(r, s(u"Changed"));
    QVERIFY(QFile::remove(r.a));
    QVERIFY(QFile::copy(r.tone, r.a));
    QFile stranger(r.a);
    QVERIFY(stranger.open(QIODevice::ReadOnly));
    const QByteArray was = stranger.readAll();
    stranger.close();
    r.run(u"edit.undo");
    QVERIFY(stranger.open(QIODevice::ReadOnly));
    QCOMPARE(stranger.readAll(), was);
    QCOMPARE(readTags(r.b).tags.first(s(u"Album")), beforeB);
    QCOMPARE(r.tools->lastMessage(),
        s(u"Undid Edit Tags: 1 file put back; 1 changed or moved since, and left as it is (a new "
          u"edit drops this)"));
}

QTEST_MAIN(ToolsShellUndoMovedTest)
#include "tst_ui_toolsshell_undo_moved.moc"
