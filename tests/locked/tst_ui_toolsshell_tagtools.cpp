// LOCKED ACCEPTANCE TEST (the tools shell: the two tagging tools open from the window). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/tools-shell.md (TS-TOOLS). Runs on
// the offscreen platform with the real window, library and shells.
// Instead of a person, a function is handed the dialog and works it.
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
#include "tagtools/tagtooldialog.h"
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

}

class ToolsShellTagToolsTest : public QObject {
    Q_OBJECT
private slots:
    void commands();
    void searchAndReplace();
    void capitalise();
    void closedWithoutApplying();
    void needsASelection();
};

void ToolsShellTagToolsTest::commands()
{
    // TS-COMMANDS: the two tagging tools are the shell's while it exists.
    Rig r;
    (void)&sorted;
    for (const char16_t *id : { u"tools.tagging.searchReplace", u"tools.tagging.capitalise" })
        QVERIFY2(r.actions.isImplemented(s(id)), qPrintable(s(id)));
    // The tools that have no window yet stay as they are.
    for (const char16_t *id : { u"tools.tagging.removeTags", u"tools.tagging.renumber", u"tools.tagging.inferFromFilename" })
        QVERIFY2(!r.actions.isImplemented(s(id)), qPrintable(s(id)));
    r.tools.reset();
    for (const char16_t *id : { u"tools.tagging.searchReplace", u"tools.tagging.capitalise" })
        QVERIFY2(!r.actions.isImplemented(s(id)), qPrintable(s(id)));
}

void ToolsShellTagToolsTest::searchAndReplace()
{
    // TS-TOOLS: the dialog gets the selected tracks; what it wrote is read again, and only that.
    Rig r;
    QVERIFY(r.lib->trackList());
    r.select({ r.a, r.b });
    QCOMPARE(r.lib->selectedTrackIds().size(), 2);
    QCOMPARE(r.title(r.a), s(u"Flac Title"));
    QCOMPARE(r.title(r.b), s(u"M4A Title"));
    QSignalSpy changed(r.library.get(), &Library::tracksUpdated);
    QList<qint64> savedIds;
    int savedSignals = 0;
    r.work = [&](QDialog *dialog) {
        auto *tool = qobject_cast<TagToolDialog *>(dialog);
        QVERIFY(tool);
        QCOMPARE(tool->parentWidget(), &r.window);
        QVERIFY(qobject_cast<SearchReplacePanel *>(tool->panel()));
        QCOMPARE(tool->pendingChanges().size(), 2); // both tracks were handed over
        QObject::connect(tool, &TagToolDialog::saved, tool, [&](const QList<qint64> &ids) {
            savedIds = ids;
            ++savedSignals;
        });
        type(tool, u"findEdit", s(u"Flac Title"));
        type(tool, u"replaceEdit", s(u"Lossless"));
        auto *matchCase = tool->findChild<QCheckBox *>(s(u"matchCaseBox"));
        QVERIFY(matchCase && !matchCase->isChecked());
        matchCase->setChecked(true);
        QVERIFY(tool->apply());
    };
    r.run(u"tools.tagging.searchReplace");
    QCOMPARE(r.presented, 1);
    QCOMPARE(r.kind, s(u"linuxbee::ui::TagToolDialog"));
    QVERIFY(r.last.isNull()); // the dialog is gone when the user is done
    QCOMPARE(savedSignals, 1);
    QCOMPARE(savedIds, QList<qint64>({ idOf(r, r.a) })); // only the one file had the text
    // The file was written, and the library has read it again.
    QCOMPARE(readTags(r.a).tags.first(s(u"Title")), s(u"Lossless"));
    QCOMPARE(r.title(r.a), s(u"Lossless"));
    QCOMPARE(r.title(r.b), s(u"M4A Title"));
    QCOMPARE(updated(changed), savedIds); // one reading for the file written, none for the other
    QCOMPARE(r.tools->lastMessage(), s(u"Saved tags of 1 track"));
    QCOMPARE(r.window.statusBar()->currentMessage(), r.tools->lastMessage());

    // The options are remembered through the shell's settings (TT-REMEMBER): the next time, the box is ticked.
    bool ticked = false;
    r.work = [&](QDialog *dialog) {
        if (auto *box = dialog->findChild<QCheckBox *>(s(u"matchCaseBox")))
            ticked = box->isChecked();
    };
    r.run(u"tools.tagging.searchReplace");
    QCOMPARE(r.presented, 2);
    QVERIFY(ticked);

    // Two files at once: the plural.
    r.work = [&](QDialog *dialog) {
        type(dialog, u"findEdit", s(u"Title"));
        type(dialog, u"replaceEdit", s(u"Name"));
        QVERIFY(qobject_cast<TagToolDialog *>(dialog)->apply());
    };
    r.select({ r.b, r.tone, r.a });
    const QString toneBefore = r.title(r.tone);
    const int expected = toneBefore.contains(s(u"Title")) ? 2 : 1; // a's title is "Lossless" now
    changed.clear();
    r.run(u"tools.tagging.searchReplace");
    QCOMPARE(r.title(r.b), s(u"M4A Name"));
    QVERIFY(updated(changed).contains(idOf(r, r.b)));
    QVERIFY(updated(changed).size() >= expected && updated(changed).size() <= 3);
    QCOMPARE(r.tools->lastMessage(),
        updated(changed).size() == 1 ? s(u"Saved tags of 1 track") : QStringLiteral("Saved tags of %1 tracks").arg(updated(changed).size()));
}

void ToolsShellTagToolsTest::capitalise()
{
    // TS-TOOLS: the other tool, through the same dialog.
    Rig r;
    r.select({ r.b });
    r.work = [&](QDialog *dialog) {
        auto *tool = qobject_cast<TagToolDialog *>(dialog);
        QVERIFY(tool);
        QVERIFY(qobject_cast<CapitalisePanel *>(tool->panel()));
        QCOMPARE(tool->pendingChanges().size(), 1);
        auto *style = tool->findChild<QComboBox *>(s(u"styleCombo"));
        QVERIFY(style);
        style->setCurrentIndex(style->findData(s(u"upper")));
        QVERIFY(tool->apply());
    };
    r.run(u"tools.tagging.capitalise");
    QCOMPARE(r.presented, 1);
    QCOMPARE(r.kind, s(u"linuxbee::ui::TagToolDialog"));
    QVERIFY(r.last.isNull());
    QCOMPARE(r.title(r.b), s(u"M4A TITLE"));
    QCOMPARE(readTags(r.b).tags.first(s(u"Title")), s(u"M4A TITLE"));
    QCOMPARE(r.title(r.a), s(u"Flac Title")); // not selected: not touched
    QCOMPARE(r.tools->lastMessage(), s(u"Saved tags of 1 track"));
}

void ToolsShellTagToolsTest::closedWithoutApplying()
{
    // TS-TOOLS: a dialog that is closed without applying changes nothing and says nothing.
    Rig r;
    r.select({ r.a });
    QSignalSpy changed(r.library.get(), &Library::tracksUpdated);
    r.work = [&](QDialog *dialog) {
        type(dialog, u"findEdit", s(u"Flac"));
        type(dialog, u"replaceEdit", s(u"Changed"));
        if (auto *box = dialog->findChild<QCheckBox *>(s(u"wholeWordBox")))
            box->setChecked(true);
        dialog->reject();
    };
    r.run(u"tools.tagging.searchReplace");
    QCOMPARE(r.presented, 1);
    QCOMPARE(r.title(r.a), s(u"Flac Title"));
    QCOMPARE(readTags(r.a).tags.first(s(u"Title")), s(u"Flac Title"));
    QCOMPARE(changed.count(), 0);
    QCOMPARE(r.tools->lastMessage(), QString());
    // And what was chosen is not remembered.
    bool ticked = true;
    r.work = [&](QDialog *dialog) {
        if (auto *box = dialog->findChild<QCheckBox *>(s(u"wholeWordBox")))
            ticked = box->isChecked();
    };
    r.run(u"tools.tagging.searchReplace");
    QCOMPARE(r.presented, 2);
    QVERIFY(!ticked);
}

void ToolsShellTagToolsTest::needsASelection()
{
    // TS-SELECTION: with nothing selected no dialog is made.
    Rig r;
    QVERIFY(r.lib->trackList());
    for (const char16_t *id : { u"tools.tagging.searchReplace", u"tools.tagging.capitalise" }) {
        r.window.statusBar()->clearMessage();
        r.run(id);
        QCOMPARE(r.presented, 0);
        QCOMPARE(r.tools->lastMessage(), s(u"Select one or more tracks first"));
        QCOMPARE(r.window.statusBar()->currentMessage(), r.tools->lastMessage());
    }
}

QTEST_MAIN(ToolsShellTagToolsTest)
#include "tst_ui_toolsshell_tagtools.moc"
