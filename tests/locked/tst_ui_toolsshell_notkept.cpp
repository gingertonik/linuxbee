// LOCKED ACCEPTANCE TEST (the tools shell says which values an ID3v2.3 file could not keep). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/tools-shell.md (TS-TAGS, "Not kept") and docs/spec/tag-writing.md (W-ID3VER).
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

// An ID3v2.3 MP3 beside the rig's files, in the library.
QString addV23(Rig &r, const char *as)
{
    const QString path = r.put("tags/v23.mp3", as);
    if (r.library->addFile(path, QStringLiteral("Music"), r.now) == 0)
        QTest::qFail("the MP3 did not go into the library", __FILE__, __LINE__);
    return path;
}

// Sets the year of the selected tracks in the tag editor.
void editYear(Rig &r, const QString &year)
{
    r.work = [&r, year](QDialog *dialog) {
        auto *editor = qobject_cast<TagEditorDialog *>(dialog);
        if (!editor)
            QFAIL("not the tag editor");
        type(editor, u"field.Year", year);
        QVERIFY(editor->save());
    };
    r.run(u"file.editTags");
}

}

class ToolsShellNotKeptTest : public QObject {
    Q_OBJECT
private slots:
    void theMessageNamesThem();
    void nothingLostNothingSaid();
};

void ToolsShellNotKeptTest::theMessageNamesThem()
{
    // TS-TAGS, not kept: ID3v2.3 holds a year, not a year and a month. The FLAC keeps it; the MP3 cannot.
    (void)&sorted;
    (void)&album;
    (void)&idOf;
    (void)&updated;
    Rig r;
    NEED_UNDO(r);
    const QString one = addV23(r, "music/one.mp3");
    const QString two = addV23(r, "music/two.mp3");
    r.select({ one, r.a });
    editYear(r, s(u"2019-03"));
    QCOMPARE(readTags(r.a).tags.first(s(u"Year")), s(u"2019-03"));
    QCOMPARE(readTags(one).tags.first(s(u"Year")), s(u"2019"));
    QCOMPARE(r.tools->lastMessage(), s(u"Saved tags of 2 tracks; not kept in ID3v2.3: Year (1 file)"));
    QCOMPARE(r.window.statusBar()->currentMessage(), r.tools->lastMessage());
    // Two such files: counted, the name given once.
    r.select({ one, two, r.a });
    editYear(r, s(u"2018-07"));
    QCOMPARE(r.tools->lastMessage(), s(u"Saved tags of 3 tracks; not kept in ID3v2.3: Year (2 files)"));
    // The next save has nothing of the last one's.
    r.select({ one });
    editAlbum(r, s(u"Plain Album"));
    QCOMPARE(r.tools->lastMessage(), s(u"Saved tags of 1 track"));
    // Undo is not a save of the user's and names nothing.
    r.select({ two });
    editYear(r, s(u"2017-01"));
    QCOMPARE(r.tools->lastMessage(), s(u"Saved tags of 1 track; not kept in ID3v2.3: Year (1 file)"));
    r.run(u"edit.undo");
    QCOMPARE(r.tools->lastMessage(), s(u"Undid Edit Tags: 1 file put back"));
}

void ToolsShellNotKeptTest::nothingLostNothingSaid()
{
    // TS-TAGS: a year alone fits; so does anything in a FLAC. The message is the plain one.
    Rig r;
    NEED_UNDO(r);
    const QString one = addV23(r, "music/one.mp3");
    r.select({ one, r.a });
    editYear(r, s(u"2019"));
    QCOMPARE(r.tools->lastMessage(), s(u"Saved tags of 2 tracks"));
    r.select({ r.a, r.tone });
    editYear(r, s(u"2019-03-07"));
    QCOMPARE(r.tools->lastMessage(), s(u"Saved tags of 2 tracks"));
    // A tagging tool goes the same way: its saves are plain here.
    r.select({ one });
    editAlbum(r, s(u"An Album"));
    QCOMPARE(r.tools->lastMessage(), s(u"Saved tags of 1 track"));
}

QTEST_MAIN(ToolsShellNotKeptTest)
#include "tst_ui_toolsshell_notkept.moc"
