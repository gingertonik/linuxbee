// LOCKED ACCEPTANCE TEST (the tools shell: Remove Tags, Renumber Tracks and Infer Tags From Filename open from the window). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/tools-shell.md (TS-COMMANDS,
// TS-TOOLS, TS-SELECTION). Runs on the offscreen platform with the real
// window, library and shells; the files written are copies of fixtures in
// a temporary folder. Instead of a person, a function is handed the dialog.
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QSpinBox>
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
#include "tagtools/inferpanel.h"
#include "tagtools/removetagspanel.h"
#include "tagtools/renumberpanel.h"
#include "tagtools/searchreplacepanel.h"
#include "tagtools/tagtooldialog.h"
#include "tracklist/tracklistmodel.h"
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

const char16_t *const kThree[] = { u"tools.tagging.removeTags", u"tools.tagging.renumber", u"tools.tagging.inferFromFilename" };

QString field(const Rig &r, const QString &path, const char16_t *name)
{
    const auto t = r.library->trackByPath(path);
    return t ? t->tags.first(s(name)) : QStringLiteral("NOT IN THE LIBRARY");
}

}

class ToolsShellPanelsTest : public QObject {
    Q_OBJECT
private slots:
    void commands();
    void removeTags();
    void renumberInTheOrderShown();
    void inferFromFilename();
    void needsASelection();
};

void ToolsShellPanelsTest::commands()
{
    // TS-COMMANDS: the three tools are the shell's while it exists.
    Rig r;
    (void)&sorted;
    (void)&type;
    for (const char16_t *id : kThree)
        QVERIFY2(r.actions.isImplemented(s(id)), qPrintable(s(id)));
    // Auto-tagging has no window: it stays as it is.
    QVERIFY(!r.actions.isImplemented(s(u"tools.tagging.autoTagAlbum")));
    r.tools.reset();
    for (const char16_t *id : kThree)
        QVERIFY2(!r.actions.isImplemented(s(id)), qPrintable(s(id)));
}

void ToolsShellPanelsTest::removeTags()
{
    // TS-TOOLS: Remove Tags on the selected track; the library reads that file again, and only that.
    Rig r;
    r.select({ r.a });
    QCOMPARE(field(r, r.a, u"Album"), s(u"Flac Album"));
    const QString otherAlbum = field(r, r.b, u"Album");
    QSignalSpy changed(r.library.get(), &Library::tracksUpdated);
    r.work = [&](QDialog *dialog) {
        auto *tool = qobject_cast<TagToolDialog *>(dialog);
        QVERIFY(tool);
        QCOMPARE(tool->parentWidget(), &r.window);
        QVERIFY(qobject_cast<RemoveTagsPanel *>(tool->panel()));
        QCOMPARE(tool->pendingChanges().size(), 1);
        auto *fields = tool->findChild<QListWidget *>(s(u"fieldList"));
        QVERIFY(fields);
        const QList<QListWidgetItem *> album = fields->findItems(s(u"Album"), Qt::MatchExactly);
        QCOMPARE(album.size(), 1);
        QCOMPARE(album.first()->checkState(), Qt::Unchecked);
        album.first()->setCheckState(Qt::Checked);
        QVERIFY(tool->apply());
    };
    r.run(u"tools.tagging.removeTags");
    QCOMPARE(r.presented, 1);
    QCOMPARE(r.kind, s(u"linuxbee::ui::TagToolDialog"));
    QVERIFY(r.last.isNull());
    QCOMPARE(readTags(r.a).tags.first(s(u"Album")), QString());
    QCOMPARE(readTags(r.a).tags.first(s(u"Title")), s(u"Flac Title"));
    QCOMPARE(field(r, r.a, u"Album"), QString());
    QCOMPARE(field(r, r.b, u"Album"), otherAlbum);
    QCOMPARE(updated(changed), QList<qint64>({ idOf(r, r.a) }));
    QCOMPARE(r.tools->lastMessage(), s(u"Saved tags of 1 track"));
    QCOMPARE(r.window.statusBar()->currentMessage(), r.tools->lastMessage());

    // The next time nothing is ticked (TT-REMOVE remembers nothing).
    bool anyTicked = true;
    r.work = [&](QDialog *dialog) {
        anyTicked = false;
        if (auto *fields = dialog->findChild<QListWidget *>(s(u"fieldList")))
            for (int i = 0; i < fields->count(); ++i)
                anyTicked = anyTicked || fields->item(i)->checkState() == Qt::Checked;
    };
    r.run(u"tools.tagging.removeTags");
    QCOMPARE(r.presented, 2);
    QVERIFY(!anyTicked);
}

void ToolsShellPanelsTest::renumberInTheOrderShown()
{
    // TS-SELECTION, TT-RENUMBER: the numbers follow the order the list shows, not the order of selecting.
    Rig r;
    r.select({ r.tone, r.a, r.b });
    const QList<qint64> order = r.lib->selectedTrackIds();
    QCOMPARE(order.size(), 3);
    for (int i = 1; i < 3; ++i)
        QVERIFY(r.lib->model()->rowOf(order.at(i - 1)) < r.lib->model()->rowOf(order.at(i)));
    QSignalSpy changed(r.library.get(), &Library::tracksUpdated);
    r.work = [&](QDialog *dialog) {
        auto *tool = qobject_cast<TagToolDialog *>(dialog);
        QVERIFY(tool);
        QVERIFY(qobject_cast<RenumberPanel *>(tool->panel()));
        QCOMPARE(tool->pendingChanges().size(), 3);
        auto *start = tool->findChild<QSpinBox *>(s(u"startSpin"));
        QVERIFY(start);
        start->setValue(41); // no padding here: an M4A file stores the number, not its text
        QVERIFY(tool->apply());
    };
    r.run(u"tools.tagging.renumber");
    QCOMPARE(r.presented, 1);
    QVERIFY(r.last.isNull());
    for (int i = 0; i < 3; ++i) {
        const auto track = r.library->track(order.at(i));
        QVERIFY(track.has_value());
        QCOMPARE(track->tags.first(s(u"Track#")), QString::number(41 + i));
        QCOMPARE(readTags(track->path).tags.first(s(u"Track#")), QString::number(41 + i));
    }
    QList<qint64> read = updated(changed);
    std::sort(read.begin(), read.end());
    QList<qint64> all = order;
    std::sort(all.begin(), all.end());
    QCOMPARE(read, all);
    QCOMPARE(r.tools->lastMessage(), s(u"Saved tags of 3 tracks"));
}

void ToolsShellPanelsTest::inferFromFilename()
{
    // TS-TOOLS: the panel is given each track's path.
    Rig r;
    r.select({ r.a });
    QCOMPARE(r.title(r.a), s(u"Flac Title"));
    r.work = [&](QDialog *dialog) {
        auto *tool = qobject_cast<TagToolDialog *>(dialog);
        QVERIFY(tool);
        QVERIFY(qobject_cast<InferPanel *>(tool->panel()));
        auto *pattern = tool->findChild<QLineEdit *>(s(u"patternEdit"));
        auto *blank = tool->findChild<QCheckBox *>(s(u"blankOnlyBox"));
        QVERIFY(pattern && blank);
        pattern->setText(s(u"<Album>/<Title>"));
        blank->setChecked(false);
        QVERIFY(tool->apply());
    };
    r.run(u"tools.tagging.inferFromFilename");
    QCOMPARE(r.presented, 1);
    QVERIFY(r.last.isNull());
    QCOMPARE(r.title(r.a), s(u"a")); // the file is music/a.flac
    QCOMPARE(field(r, r.a, u"Album"), s(u"music"));
    QCOMPARE(readTags(r.a).tags.first(s(u"Title")), s(u"a"));
    QCOMPARE(r.tools->lastMessage(), s(u"Saved tags of 1 track"));
    // The options went to the shell's settings.
    r.settings.sync();
    QCOMPARE(r.settings.value(s(u"tagtools/infer/pattern")).toString(), s(u"<Album>/<Title>"));
}

void ToolsShellPanelsTest::needsASelection()
{
    // TS-SELECTION: with nothing selected no dialog is made.
    Rig r;
    for (const char16_t *id : kThree) {
        r.run(id);
        QCOMPARE(r.presented, 0);
        QCOMPARE(r.tools->lastMessage(), s(u"Select one or more tracks first"));
    }
}

QTEST_MAIN(ToolsShellPanelsTest)
#include "tst_ui_toolsshell_panels.moc"
