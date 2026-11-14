// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/tools-shell.md (TS-x). Runs on the
// offscreen platform, end to end: the real dialogs, the real tag writer,
// organiser and converter, on copies of the fixtures in a temporary folder.
// Instead of a person, a function is handed the dialog and works it.
#include <QAction>
#include <QLineEdit>
#include <QPointer>
#include <QSettings>
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

}

class ToolsShellTest : public QObject {
    Q_OBJECT
private slots:
    void structure();
    void needsASelection();
    void editTags();
    void editTagsCancelled();
    void organise();
    void organiseCopyLeavesTheLibrary();
    void convert();
    void preferences();
    void extraPages();
    void gone();
};

void ToolsShellTest::structure()
{
    // TS-COMMANDS
    Rig r;
    for (const char16_t *id : { u"file.editTags", u"file.organise", u"tools.convert", u"edit.preferences" })
        QVERIFY2(r.actions.isImplemented(s(id)), qPrintable(s(id)));
    QCOMPARE(r.tools->lastMessage(), QString());
    QCOMPARE(r.presented, 0);
}

void ToolsShellTest::needsASelection()
{
    // TS-SELECTION
    Rig r;
    for (const char16_t *id : { u"file.editTags", u"file.organise", u"tools.convert" }) {
        r.window.statusBar()->clearMessage();
        r.run(id);
        QCOMPARE(r.presented, 0);
        QCOMPARE(r.window.statusBar()->currentMessage(), s(u"Select one or more tracks first"));
        QCOMPARE(r.tools->lastMessage(), s(u"Select one or more tracks first"));
    }
}

void ToolsShellTest::editTags()
{
    // TS-TAGS
    Rig r;
    QVERIFY(r.lib->trackList());
    r.select({ r.a, r.b });
    QCOMPARE(r.lib->selectedTrackIds().size(), 2);
    QList<qint64> savedIds;
    r.work = [&](QDialog *dialog) {
        auto *editor = qobject_cast<TagEditorDialog *>(dialog);
        QVERIFY(editor);
        QCOMPARE(editor->parentWidget(), &r.window);
        QCOMPARE(editor->pendingChanges().size(), 2); // both tracks were handed over, with their tags
        QObject::connect(editor, &TagEditorDialog::saved, editor, [&](const QList<qint64> &ids) { savedIds = ids; });
        type(editor, u"field.Album", s(u"A New Album"));
        QVERIFY(editor->save());
    };
    r.run(u"file.editTags");
    QCOMPARE(r.presented, 1);
    QCOMPARE(r.kind, s(u"linuxbee::ui::TagEditorDialog"));
    QVERIFY(r.last.isNull()); // the dialog is gone when the user is done
    QCOMPARE(sorted({ QString::number(savedIds.value(0)), QString::number(savedIds.value(1)) }),
        sorted({ QString::number(r.library->trackByPath(r.a)->id), QString::number(r.library->trackByPath(r.b)->id) }));
    // The files were written, and the library has read them again.
    QCOMPARE(r.library->trackByPath(r.a)->tags.first(s(u"Album")), s(u"A New Album"));
    QCOMPARE(r.library->trackByPath(r.b)->tags.first(s(u"Album")), s(u"A New Album"));
    QCOMPARE(r.title(r.a), s(u"Flac Title")); // what was not edited is as it was
    QCOMPARE(readTags(r.a).tags.first(s(u"Album")), s(u"A New Album"));
    QCOMPARE(r.tools->lastMessage(), s(u"Saved tags of 2 tracks"));
    QCOMPARE(r.window.statusBar()->currentMessage(), r.tools->lastMessage());
    // One track: the singular.
    r.select({ r.a });
    r.work = [&](QDialog *dialog) {
        type(dialog, u"field.Title", s(u"Renamed"));
        QVERIFY(qobject_cast<TagEditorDialog *>(dialog)->save());
    };
    r.run(u"file.editTags");
    QCOMPARE(r.title(r.a), s(u"Renamed"));
    QCOMPARE(r.tools->lastMessage(), s(u"Saved tags of 1 track"));
}

void ToolsShellTest::editTagsCancelled()
{
    // TS-TAGS: a dialog the user closes changes nothing and says nothing.
    Rig r;
    r.select({ r.a });
    r.work = [&](QDialog *dialog) {
        type(dialog, u"field.Title", s(u"Never saved"));
        dialog->reject();
    };
    r.run(u"file.editTags");
    QCOMPARE(r.presented, 1);
    QCOMPARE(r.title(r.a), s(u"Flac Title"));
    QCOMPARE(readTags(r.a).tags.first(s(u"Title")), s(u"Flac Title"));
    QCOMPARE(r.tools->lastMessage(), QString());
    QVERIFY(r.last.isNull());
}

void ToolsShellTest::organise()
{
    // TS-ORGANISE: files that were moved are followed by the library.
    Rig r;
    const QString out = r.made(s(u"sorted"));
    const qint64 idA = r.library->trackByPath(r.a)->id;
    r.select({ r.a, r.b });
    r.work = [&](QDialog *dialog) {
        auto *organiser = qobject_cast<OrganiseDialog *>(dialog);
        QVERIFY(organiser);
        QCOMPARE(organiser->parentWidget(), &r.window);
        type(organiser, u"targetEdit", out);
        type(organiser, u"templateEdit", s(u"<Title>"));
        QCOMPARE(organiser->preview().size(), 2);
        QVERIFY(organiser->run());
    };
    r.run(u"file.organise");
    QCOMPARE(r.kind, s(u"linuxbee::ui::OrganiseDialog"));
    QVERIFY(r.last.isNull());
    const QString movedA = out + s(u"/Flac Title.flac");
    const QString movedB = out + s(u"/M4A Title.m4a");
    QVERIFY(QFile::exists(movedA));
    QVERIFY(!QFile::exists(r.a));
    QVERIFY(!r.library->trackByPath(r.a).has_value());
    QVERIFY(r.library->trackByPath(movedA).has_value());
    QCOMPARE(r.library->trackByPath(movedA)->id, idA); // the same track, with its history
    QVERIFY(r.library->trackByPath(movedB).has_value());
    QCOMPARE(r.library->count(), 3);
    QCOMPARE(r.tools->lastMessage(), s(u"Organised: 2 done, 0 skipped, 0 failed"));
    QCOMPARE(sorted(r.lib->shownPaths()), sorted({ movedA, movedB, r.tone })); // and the list shows the new places
    // The dialog remembered its options in the settings it was given.
    QCOMPARE(r.settings.value(s(u"organise/target")).toString(), out);
}

void ToolsShellTest::organiseCopyLeavesTheLibrary()
{
    // TS-ORGANISE: a copy is a new file the library does not know; the original stays where it is.
    Rig r;
    const QString out = r.made(s(u"copies"));
    r.settings.setValue(s(u"organise/copy"), s(u"true")); // text, as a stored value is read back
    r.settings.setValue(s(u"organise/target"), out);
    r.settings.setValue(s(u"organise/template"), s(u"<Title>"));
    r.select({ r.a });
    r.work = [&](QDialog *dialog) {
        auto *organiser = qobject_cast<OrganiseDialog *>(dialog);
        QVERIFY(organiser);
        QVERIFY(organiser->options().mode == OrganiseOptions::Mode::Copy);
        QVERIFY(organiser->run());
    };
    r.run(u"file.organise");
    QVERIFY(QFile::exists(out + s(u"/Flac Title.flac")));
    QVERIFY(QFile::exists(r.a));
    QVERIFY(r.library->trackByPath(r.a).has_value());
    QVERIFY(!r.library->trackByPath(out + s(u"/Flac Title.flac")).has_value());
    QCOMPARE(r.library->count(), 3);
    QCOMPARE(r.tools->lastMessage(), s(u"Organised: 1 done, 0 skipped, 0 failed"));
}

void ToolsShellTest::convert()
{
    // TS-CONVERT
    Rig r;
    const QString out = r.made(s(u"converted"));
    r.select({ r.tone });
    QStringList results;
    r.work = [&](QDialog *dialog) {
        auto *converter = qobject_cast<ConvertDialog *>(dialog);
        QVERIFY(converter);
        QCOMPARE(converter->parentWidget(), &r.window);
        QCOMPARE(converter->plan().size(), 1);
        QCOMPARE(converter->plan().at(0).source, r.tone);
        QObject::connect(converter, &ConvertDialog::converted, converter, [&](const QStringList &paths) { results = paths; });
        type(converter, u"folderEdit", out);
        QVERIFY(converter->run());
        QVERIFY(QTest::qWaitFor([&] { return converter->result() == QDialog::Accepted; }, 30000));
    };
    r.run(u"tools.convert");
    QCOMPARE(r.kind, s(u"linuxbee::ui::ConvertDialog"));
    QVERIFY(r.last.isNull());
    QCOMPARE(results.size(), 1);
    QVERIFY(results.at(0).startsWith(out + QLatin1Char('/')));
    QVERIFY(QFileInfo(results.at(0)).size() > 1000);
    QCOMPARE(r.tools->lastMessage(), s(u"Converted 1 file"));
    QCOMPARE(r.window.statusBar()->currentMessage(), r.tools->lastMessage());
    // The source is untouched and the result is not put into the library.
    QVERIFY(QFile::exists(r.tone));
    QCOMPARE(r.library->count(), 3);
    QVERIFY(!r.library->trackByPath(results.at(0)).has_value());
}

void ToolsShellTest::preferences()
{
    // TS-PREFERENCES: needs no selection; has the Hotkeys page over the window's own commands.
    Rig r;
    r.work = [&](QDialog *dialog) {
        auto *prefs = qobject_cast<PreferencesDialog *>(dialog);
        QVERIFY(prefs);
        QCOMPARE(prefs->parentWidget(), &r.window);
        QCOMPARE(prefs->pages(), QStringList({ s(u"Hotkeys") }));
        auto *page = prefs->findChild<HotkeysPage *>();
        QVERIFY(page);
        QVERIFY(page->assign(s(u"help.about"), QKeySequence(s(u"F1"))).status == ShortcutResult::Status::Ok);
        QVERIFY(prefs->apply());
        prefs->accept();
    };
    r.run(u"edit.preferences");
    QCOMPARE(r.presented, 1);
    QCOMPARE(r.kind, s(u"linuxbee::ui::PreferencesDialog"));
    QVERIFY(r.last.isNull());
    QCOMPARE(r.actions.shortcut(s(u"help.about")), QKeySequence(s(u"F1")));
    r.settings.sync();
    QCOMPARE(r.settings.value(s(u"hotkeys/help.about")).toString(), s(u"F1")); // stored at once
    QCOMPARE(r.tools->lastMessage(), QString());
}

namespace {
class NotePage : public PreferencesPage {
public:
    NotePage(QString title, int *applied, int *alive)
        : name(std::move(title))
        , applied(applied)
        , alive(alive)
    {
        ++*alive;
    }
    ~NotePage() override { --*alive; }
    QString title() const override { return name; }
    void load() override { }
    QString apply() override
    {
        ++*applied;
        return { };
    }
    bool isModified() const override { return true; }
    QString name;
    int *applied;
    int *alive;
};
}

void ToolsShellTest::extraPages()
{
    // TS-PREFERENCES: pages of other plans.
    Rig r;
    QCOMPARE(r.tools->preferencesPages(), QStringList({ s(u"Hotkeys") }));
    int applied = 0, alive = 0, made = 0;
    r.tools->addPreferencesPage([&] {
        ++made;
        return new NotePage(s(u"Second"), &applied, &alive);
    });
    r.tools->addPreferencesPage({ }); // nothing
    r.tools->addPreferencesPage([]() -> PreferencesPage * { return nullptr; }); // nothing
    r.tools->addPreferencesPage([&] { return new NotePage(s(u"Third"), &applied, &alive); });
    QCOMPARE(r.tools->preferencesPages(), QStringList({ s(u"Hotkeys"), s(u"Second"), s(u"Third") }));
    QCOMPARE(r.presented, 0); // asking for the titles opens nothing
    QCOMPARE(alive, 0); // and leaves nothing behind
    const int before = made;
    for (int round = 1; round <= 2; ++round) {
        r.work = [&](QDialog *dialog) {
            auto *prefs = qobject_cast<PreferencesDialog *>(dialog);
            QVERIFY(prefs);
            QCOMPARE(prefs->pages(), QStringList({ s(u"Hotkeys"), s(u"Second"), s(u"Third") }));
            QCOMPARE(alive, 2);
            QVERIFY(prefs->apply());
            prefs->accept();
        };
        r.run(u"edit.preferences");
        QCOMPARE(r.presented, round);
        QCOMPARE(made, before + round); // a new page each time
        QCOMPARE(applied, 2 * round);
        QCOMPARE(alive, 0); // the dialog owned them
    }
}

void ToolsShellTest::gone()
{
    Rig r;
    r.tools.reset();
    for (const char16_t *id : { u"file.editTags", u"file.organise", u"tools.convert", u"edit.preferences" })
        QVERIFY2(!r.actions.isImplemented(s(id)), qPrintable(s(id)));
}

QTEST_MAIN(ToolsShellTest)
#include "tst_ui_toolsshell.moc"
