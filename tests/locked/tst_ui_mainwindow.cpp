// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/main-window.md (H-x for commands and
// hotkeys, W-x for the window). W-MENU and W-HOT are the MusicBee wiki's menu
// pages and its "Hotkeys" page. Runs on the offscreen platform.
#include <QAction>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QSignalSpy>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QtTest>

#include "actions/actionregistry.h"
#include "mainwindow/mainwindow.h"

using linuxbee::ui::ActionRegistry;
using linuxbee::ui::MainWindow;
using linuxbee::ui::ShortcutResult;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

QKeySequence key(const char *text)
{
    return QKeySequence(QLatin1String(text), QKeySequence::PortableText);
}

// The texts of a menu's entries, "-" for a separator, ">Title" for a submenu.
QStringList entries(const QMenu *menu)
{
    QStringList out;
    if (!menu)
        return { QStringLiteral("NO SUCH MENU") };
    for (const QAction *a : menu->actions()) {
        if (a->isSeparator())
            out.append(QStringLiteral("-"));
        else if (a->menu())
            out.append(QLatin1Char('>') + a->menu()->title());
        else
            out.append(a->objectName());
    }
    return out;
}

struct Fixture {
    QTemporaryDir dir;
    QSettings settings;
    ActionRegistry actions;
    Fixture()
        : settings(dir.path() + QStringLiteral("/test.ini"), QSettings::IniFormat)
    {
    }
};

}

class MainWindowTest : public QObject {
    Q_OBJECT
private slots:
    void catalogue();
    void registry();
    void implement();
    void shortcuts();
    void shortcutStore();
    void menus();
    void windowStructure();
    void panelsAndPages();
    void viewActions();
    void layoutIsRemembered();
    void layoutSurvivesNonsense();
};

void MainWindowTest::catalogue()
{
    // H-CATALOGUE: what must hold for the table, whatever is in it.
    const QList<linuxbee::ui::ActionInfo> all = linuxbee::ui::actionCatalogue();
    QVERIFY(all.size() >= 70);
    QSet<QString> ids, keys;
    const QRegularExpression idShape(QStringLiteral("^[a-z][A-Za-z0-9]*(\\.[A-Za-z0-9]+)+$"));
    const QStringList tops = { s(u"File"), s(u"Edit"), s(u"View"), s(u"Controls"), s(u"Tools"), s(u"Help"), s(u"Context") };
    for (const auto &info : all) {
        QVERIFY2(idShape.match(info.id).hasMatch(), qPrintable(info.id));
        QVERIFY2(!ids.contains(info.id), qPrintable(info.id));
        ids.insert(info.id);
        QVERIFY2(!info.text.trimmed().isEmpty(), qPrintable(info.id));
        QVERIFY2(info.menu.isEmpty() || tops.contains(info.menu.section(QLatin1Char('/'), 0, 0)), qPrintable(info.id));
        if (info.defaultShortcut.isEmpty())
            continue;
        const QKeySequence seq(info.defaultShortcut, QKeySequence::PortableText);
        QVERIFY2(seq.count() == 1, qPrintable(info.id));
        QCOMPARE(seq.toString(QKeySequence::PortableText), info.defaultShortcut); // written the way Qt writes it
        QVERIFY2(!keys.contains(info.defaultShortcut), qPrintable(info.id + s(u" shares ") + info.defaultShortcut));
        keys.insert(info.defaultShortcut);
    }
    // W-HOT: MusicBee's documented defaults, where linuxBee has the command.
    const QHash<QString, QString> documented = { { s(u"edit.preferences"), s(u"Ctrl+O") },
        { s(u"edit.copyTags"), s(u"Ctrl+C") }, { s(u"edit.find"), s(u"Ctrl+F") },
        { s(u"file.editTags"), s(u"Shift+Return") }, { s(u"edit.pasteTags"), s(u"Ctrl+V") },
        { s(u"edit.selectAll"), s(u"Ctrl+A") }, { s(u"edit.editField"), s(u"F2") }, { s(u"file.remove"), s(u"Del") },
        { s(u"file.delete"), s(u"Shift+Del") }, { s(u"file.scanFolders"), s(u"Ins") }, { s(u"view.refresh"), s(u"F5") },
        { s(u"controls.next"), s(u"Ctrl+N") }, { s(u"track.playNow"), s(u"Alt+Return") },
        { s(u"controls.playPause"), s(u"Ctrl+P") }, { s(u"controls.previous"), s(u"Ctrl+B") },
        { s(u"track.queueLast"), s(u"Ctrl+Return") }, { s(u"track.queueNext"), s(u"Ctrl+Shift+Return") },
        { s(u"controls.stop"), s(u"Ctrl+S") }, { s(u"controls.volumeDown"), s(u"Ctrl+Alt+Down") },
        { s(u"controls.mute"), s(u"Ctrl+Alt+V") }, { s(u"controls.volumeUp"), s(u"Ctrl+Alt+Up") },
        { s(u"playing.rate.3"), s(u"Ctrl+Alt+3") }, { s(u"playing.rate.none"), s(u"Ctrl+Alt+-") },
        { s(u"track.rate.0"), s(u"Alt+0") }, { s(u"track.rate.5"), s(u"Alt+5") }, { s(u"track.rate.none"), s(u"Alt+-") },
        { s(u"tools.analyseVolume"), s(u"Ctrl+Shift+V") }, { s(u"file.organise"), s(u"Ctrl+R") },
        { s(u"tools.tagging.autoTagAlbum"), s(u"Ctrl+Shift+T") }, { s(u"tools.tagging.searchReplace"), s(u"Ctrl+G") },
        { s(u"view.fullScreen"), s(u"F11") } };
    ActionRegistry registry;
    for (auto it = documented.constBegin(); it != documented.constEnd(); ++it)
        QVERIFY2(registry.info(it.key()).defaultShortcut == it.value(), qPrintable(it.key()));
}

void MainWindowTest::registry()
{
    // H-CATALOGUE: one action per entry, ready to use.
    ActionRegistry registry;
    const auto all = linuxbee::ui::actionCatalogue();
    QStringList ids;
    for (const auto &info : all)
        ids.append(info.id);
    QCOMPARE(registry.ids(), ids);
    for (const auto &info : all) {
        QAction *a = registry.action(info.id);
        QVERIFY2(a, qPrintable(info.id));
        QCOMPARE(a->objectName(), info.id);
        QCOMPARE(a->text(), info.text);
        QCOMPARE(a->isCheckable(), info.checkable);
        QCOMPARE(a->shortcut(), QKeySequence(info.defaultShortcut, QKeySequence::PortableText));
        QCOMPARE(registry.shortcut(info.id), a->shortcut());
        QCOMPARE(registry.defaultShortcut(info.id), a->shortcut());
        QVERIFY(a->shortcutContext() == Qt::WindowShortcut);
        QVERIFY2(!a->isEnabled(), qPrintable(info.id)); // H-IMPLEMENT: nothing works until something implements it
        QVERIFY(!registry.isImplemented(info.id));
        QCOMPARE(registry.info(info.id).text, info.text);
        QVERIFY(a->parent() == &registry);
    }
    QVERIFY(!registry.action(s(u"no.such")));
    QVERIFY(registry.info(s(u"no.such")).id.isEmpty());
    QVERIFY(registry.shortcut(s(u"no.such")).isEmpty());
    QVERIFY(registry.action(s(u"controls.playPause")) == registry.action(s(u"controls.playPause")));
    // Two registries do not share actions.
    ActionRegistry other;
    QVERIFY(other.action(s(u"file.quit")) != registry.action(s(u"file.quit")));
}

void MainWindowTest::implement()
{
    // H-IMPLEMENT
    ActionRegistry registry;
    int calls = 0;
    auto *owner = new QObject;
    QVERIFY(registry.implement(s(u"controls.stop"), owner, [&calls] { ++calls; }));
    QAction *stop = registry.action(s(u"controls.stop"));
    QVERIFY(stop);
    QVERIFY(stop->isEnabled());
    QVERIFY(registry.isImplemented(s(u"controls.stop")));
    stop->trigger();
    stop->trigger();
    QCOMPARE(calls, 2);
    // One behaviour per command; an unknown id, a null context and an empty function are refused.
    QVERIFY(!registry.implement(s(u"controls.stop"), owner, [&calls] { calls += 100; }));
    QVERIFY(!registry.implement(s(u"no.such"), owner, [] { }));
    QVERIFY(!registry.implement(s(u"controls.next"), nullptr, [] { }));
    QVERIFY(!registry.implement(s(u"controls.next"), owner, std::function<void()>()));
    QVERIFY(!registry.action(s(u"controls.next"))->isEnabled());
    stop->trigger();
    QCOMPARE(calls, 3);
    // When the implementing object goes, the command is free again and disabled.
    delete owner;
    QVERIFY(!stop->isEnabled());
    QVERIFY(!registry.isImplemented(s(u"controls.stop")));
    stop->trigger();
    QCOMPARE(calls, 3);
    QObject second;
    QVERIFY(registry.implement(s(u"controls.stop"), &second, [&calls] { calls += 10; }));
    stop->trigger();
    QCOMPARE(calls, 13);
    // A checkable command hands its state to whoever asks the action.
    bool seen = false;
    QAction *shuffle = registry.action(s(u"controls.shuffle"));
    QVERIFY(shuffle);
    QVERIFY(registry.implement(s(u"controls.shuffle"), &second, [&seen, shuffle] { seen = shuffle->isChecked(); }));
    shuffle->trigger();
    QVERIFY(seen);
    shuffle->trigger();
    QVERIFY(!seen);
}

void MainWindowTest::shortcuts()
{
    // H-SET
    ActionRegistry registry;
    QSignalSpy changed(&registry, &ActionRegistry::shortcutChanged);
    const QString play = s(u"controls.playPause"), stop = s(u"controls.stop"), mini = s(u"view.miniPlayer");
    // A free key.
    QVERIFY(registry.setShortcut(mini, key("Ctrl+M")).status == ShortcutResult::Status::Ok);
    QCOMPARE(registry.shortcut(mini), key("Ctrl+M"));
    QCOMPARE(registry.action(mini)->shortcut(), key("Ctrl+M"));
    QCOMPARE(changed.size(), 1);
    QCOMPARE(changed.last().at(0).toString(), mini);
    // The same key again: fine, and nothing changed.
    QVERIFY(registry.setShortcut(mini, key("Ctrl+M")).status == ShortcutResult::Status::Ok);
    QCOMPARE(changed.size(), 1);
    // A key another command has: refused, with that command's id; nothing changes.
    ShortcutResult r = registry.setShortcut(mini, key("Ctrl+P"));
    QVERIFY(r.status == ShortcutResult::Status::Conflict);
    QCOMPARE(r.conflictsWith, play);
    QCOMPARE(registry.shortcut(mini), key("Ctrl+M"));
    QCOMPARE(registry.shortcut(play), key("Ctrl+P"));
    // Free it first, then it can be given away.
    QVERIFY(registry.setShortcut(play, QKeySequence()).status == ShortcutResult::Status::Ok);
    QVERIFY(registry.shortcut(play).isEmpty());
    QVERIFY(registry.setShortcut(mini, key("Ctrl+P")).status == ShortcutResult::Status::Ok);
    QCOMPARE(changed.size(), 3);
    // Now the default of Play/Pause is taken: resetting it would collide, so it stays empty.
    registry.resetShortcut(play);
    QVERIFY(registry.shortcut(play).isEmpty());
    QCOMPARE(registry.defaultShortcut(play), key("Ctrl+P"));
    registry.resetShortcut(mini); // its default is no key
    QVERIFY(registry.shortcut(mini).isEmpty());
    registry.resetShortcut(play);
    QCOMPARE(registry.shortcut(play), key("Ctrl+P"));
    // Refused: an unknown command, and a chord of two keys.
    QVERIFY(registry.setShortcut(s(u"no.such"), key("Ctrl+M")).status == ShortcutResult::Status::UnknownAction);
    QVERIFY(registry.setShortcut(stop, key("Ctrl+K, Ctrl+S")).status == ShortcutResult::Status::Invalid);
    QCOMPARE(registry.shortcut(stop), key("Ctrl+S"));
    // resetAllShortcuts puts every default back, whatever was swapped around.
    registry.setShortcut(play, QKeySequence());
    registry.setShortcut(stop, key("Ctrl+P"));
    registry.setShortcut(play, key("Ctrl+S"));
    registry.resetAllShortcuts();
    for (const auto &info : linuxbee::ui::actionCatalogue())
        QCOMPARE(registry.shortcut(info.id), QKeySequence(info.defaultShortcut, QKeySequence::PortableText));
}

void MainWindowTest::shortcutStore()
{
    // H-STORE
    QTemporaryDir dir;
    const QString file = dir.path() + s(u"/keys.ini");
    const QString play = s(u"controls.playPause"), stop = s(u"controls.stop"), mini = s(u"view.miniPlayer");
    {
        ActionRegistry registry;
        QSettings settings(file, QSettings::IniFormat);
        registry.save(settings);
        settings.beginGroup(s(u"hotkeys"));
        QVERIFY(settings.childKeys().isEmpty()); // defaults are not stored
        settings.endGroup();
        registry.setShortcut(play, QKeySequence()); // cleared
        registry.setShortcut(stop, key("Ctrl+P")); // moved onto another command's default
        registry.setShortcut(mini, key("Ctrl+M")); // given a key
        registry.save(settings);
        settings.beginGroup(s(u"hotkeys"));
        QCOMPARE(settings.childKeys().size(), 3);
        QCOMPARE(settings.value(play).toString(), QString());
        QCOMPARE(settings.value(stop).toString(), s(u"Ctrl+P"));
        QCOMPARE(settings.value(mini).toString(), s(u"Ctrl+M"));
        settings.endGroup();
        // Saving again after going back to the defaults leaves nothing behind.
        registry.resetAllShortcuts();
        registry.save(settings);
        settings.beginGroup(s(u"hotkeys"));
        QVERIFY(settings.childKeys().isEmpty());
        settings.endGroup();
        registry.setShortcut(play, QKeySequence());
        registry.setShortcut(stop, key("Ctrl+P"));
        registry.setShortcut(mini, key("Ctrl+M"));
        registry.save(settings);
        settings.setValue(s(u"other/untouched"), 7);
    }
    {
        ActionRegistry registry;
        QSettings settings(file, QSettings::IniFormat);
        registry.load(settings);
        QVERIFY(registry.shortcut(play).isEmpty());
        QCOMPARE(registry.shortcut(stop), key("Ctrl+P"));
        QCOMPARE(registry.shortcut(mini), key("Ctrl+M"));
        QCOMPARE(registry.shortcut(s(u"controls.next")), key("Ctrl+N")); // the rest keep their defaults
        QCOMPARE(settings.value(s(u"other/untouched")).toInt(), 7); // load and save keep to their own group
        // Loading replaces what was there, including changes not in the file.
        registry.setShortcut(s(u"controls.next"), key("Ctrl+J"));
        registry.load(settings);
        QCOMPARE(registry.shortcut(s(u"controls.next")), key("Ctrl+N"));
    }
    {
        // A file edited by hand: unknown ids and unreadable keys are skipped; of two commands
        // claiming one key, the earlier in the catalogue keeps it and the other gets none.
        QSettings settings(dir.path() + s(u"/odd.ini"), QSettings::IniFormat);
        settings.setValue(s(u"hotkeys/no.such"), s(u"Ctrl+M"));
        settings.setValue(s(u"hotkeys/view.miniPlayer"), s(u"Ctrl+K, Ctrl+S"));
        settings.setValue(s(u"hotkeys/controls.stop"), s(u"Ctrl+J"));
        settings.setValue(s(u"hotkeys/controls.next"), s(u"Ctrl+J"));
        settings.setValue(s(u"hotkeys/view.refresh"), s(u"Ctrl+B")); // the default of Previous Track, which the file leaves alone
        ActionRegistry registry;
        registry.load(settings);
        QVERIFY(registry.shortcut(mini).isEmpty());
        QCOMPARE(registry.shortcut(stop), key("Ctrl+J"));
        QVERIFY(registry.shortcut(s(u"controls.next")).isEmpty());
        QCOMPARE(registry.shortcut(s(u"controls.previous")), key("Ctrl+B")); // a default the file did not touch wins
        QVERIFY(registry.shortcut(s(u"view.refresh")).isEmpty());
    }
}

void MainWindowTest::menus()
{
    // H-MENU (W-MENU: File, Edit, View, Controls, Tools, Help, in that order)
    ActionRegistry registry;
    QMenuBar bar;
    registry.populateMenuBar(&bar);
    QStringList titles;
    for (const QAction *a : bar.actions())
        titles.append(a->menu() ? a->menu()->title() : QStringLiteral("?"));
    QCOMPARE(titles, QStringList({ s(u"File"), s(u"Edit"), s(u"View"), s(u"Controls"), s(u"Tools"), s(u"Help") }));
    QCOMPARE(entries(bar.findChild<QMenu *>(s(u"menu.Edit"))),
        QStringList({ s(u"edit.undo"), s(u"-"), s(u"edit.selectAll"), s(u"edit.copyTags"), s(u"edit.pasteTags"),
            s(u"edit.editField"), s(u"-"), s(u"edit.find"), s(u"-"), s(u"edit.preferences") }));
    QCOMPARE(entries(bar.findChild<QMenu *>(s(u"menu.File"))),
        QStringList({ s(u"file.addFiles"), s(u"file.scanFolders"), s(u"file.scanStop"), s(u">Library"), s(u">Playlists"), s(u"-"),
            s(u"file.editTags"), s(u"file.locate"), s(u"file.organise"), s(u"-"), s(u"file.remove"),
            s(u"file.delete"), s(u"-"), s(u"file.quit") }));
    // A separator asked for at the top of a menu is not drawn.
    QCOMPARE(entries(bar.findChild<QMenu *>(s(u"menu.File/Library"))),
        QStringList({ s(u"file.library.import"), s(u"file.library.relink"), s(u"file.library.statistics") }));
    QCOMPARE(entries(bar.findChild<QMenu *>(s(u"menu.Tools"))),
        QStringList({ s(u">Tagging Tools"), s(u"-"), s(u"tools.convert"), s(u"-"), s(u"tools.analyseVolume"),
            s(u"tools.restoreVolume"), s(u">Advanced") }));
    QCOMPARE(entries(bar.findChild<QMenu *>(s(u"menu.Controls/Replay Gain"))),
        QStringList({ s(u"controls.gain.off"), s(u"controls.gain.track"), s(u"controls.gain.album") }));
    // The entries are the registry's own actions, and every menu command is somewhere.
    QVERIFY(bar.findChild<QMenu *>(s(u"menu.Help"))->actions().contains(registry.action(s(u"help.about"))));
    for (const auto &info : linuxbee::ui::actionCatalogue()) {
        const bool inBar = !info.menu.isEmpty() && info.menu.section(QLatin1Char('/'), 0, 0) != s(u"Context");
        const QMenu *menu = bar.findChild<QMenu *>(s(u"menu.") + info.menu);
        QCOMPARE(menu != nullptr && menu->actions().contains(registry.action(info.id)), inBar);
    }
    // Context commands and hotkey-only commands are not in the bar.
    QVERIFY(!bar.findChild<QMenu *>(s(u"menu.Context")));

    // populateMenu fills a menu of the caller's with one branch of the catalogue.
    QMenu context;
    registry.populateMenu(&context, s(u"Context"));
    QCOMPARE(entries(&context),
        QStringList({ s(u"track.playNow"), s(u"track.queueNext"), s(u"track.queueLast"), s(u"-"),
            s(u"track.addToPlaylist"), s(u">Send To"), s(u">Rating") }));
    QCOMPARE(entries(context.findChild<QMenu *>(s(u"menu.Context/Rating"))),
        QStringList({ s(u"track.rate.0"), s(u"track.rate.1"), s(u"track.rate.2"), s(u"track.rate.3"),
            s(u"track.rate.4"), s(u"track.rate.5"), s(u"-"), s(u"track.rate.none") }));
    QMenu tagging;
    registry.populateMenu(&tagging, s(u"Tools/Tagging Tools"));
    QCOMPARE(tagging.actions().size(), 9); // seven commands and two separators
    QMenu nothing;
    registry.populateMenu(&nothing, s(u"No Such Menu"));
    QVERIFY(nothing.isEmpty());
}

void MainWindowTest::windowStructure()
{
    // W-FRAME
    Fixture f;
    MainWindow w(&f.actions, &f.settings);
    QCOMPARE(w.objectName(), s(u"mainWindow"));
    QCOMPARE(w.windowTitle(), s(u"linuxBee"));
    QCOMPARE(w.size(), QSize(1200, 760)); // W-LAYOUT: the first-run size
    QVERIFY(w.minimumWidth() >= 640 && w.minimumHeight() >= 400);
    auto *splitter = w.findChild<QSplitter *>(s(u"mainSplitter"));
    QVERIFY(splitter);
    QVERIFY(splitter->orientation() == Qt::Horizontal);
    QCOMPARE(splitter->count(), 3);
    QCOMPARE(splitter->widget(0)->objectName(), s(u"navigatorPanel"));
    QCOMPARE(splitter->widget(1)->objectName(), s(u"mainPanel"));
    QCOMPARE(splitter->widget(2)->objectName(), s(u"rightPanel"));
    QVERIFY(qobject_cast<QStackedWidget *>(splitter->widget(1)));
    QWidget *player = w.findChild<QWidget *>(s(u"playerBar"));
    QVERIFY(player);
    QCOMPARE(w.statusBar()->objectName(), s(u"statusBar"));
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    // The player bar spans the window below the three panels; the panels fill the width between them.
    QVERIFY(player->isVisible());
    QCOMPARE(player->width(), w.centralWidget()->width());
    QVERIFY(player->mapTo(&w, QPoint(0, 0)).y() >= splitter->mapTo(&w, QPoint(0, 0)).y() + splitter->height());
    QVERIFY(player->height() >= 48);
    QVERIFY(splitter->widget(0)->width() >= 120 && splitter->widget(2)->width() >= 120);
    QVERIFY(splitter->widget(1)->width() > splitter->widget(0)->width());
    QVERIFY(splitter->widget(1)->width() > splitter->widget(2)->width());
    // The menu bar is the registry's, and the window listens for every command's hotkey.
    QCOMPARE(w.menuBar()->actions().size(), 6);
    QVERIFY(w.menuBar()->findChild<QMenu *>(s(u"menu.File"))->actions().contains(f.actions.action(s(u"file.quit"))));
    for (const QString &id : f.actions.ids())
        QVERIFY2(w.actions().contains(f.actions.action(id)), qPrintable(id));
    // W-COMMANDS: the window implements these itself; nothing else is enabled yet.
    const QStringList own = { s(u"file.quit"), s(u"view.navigator"), s(u"view.rightPanel"), s(u"view.statusBar"),
        s(u"view.fullScreen"), s(u"help.about") };
    for (const QString &id : f.actions.ids())
        QCOMPARE(f.actions.action(id)->isEnabled(), own.contains(id));
}

void MainWindowTest::panelsAndPages()
{
    Fixture f;
    MainWindow w(&f.actions, &f.settings);
    // W-PANELS
    QVERIFY(!w.panelWidget(MainWindow::Panel::Navigator));
    auto *first = new QWidget;
    w.setPanelWidget(MainWindow::Panel::Navigator, first);
    QCOMPARE(w.panelWidget(MainWindow::Panel::Navigator), first);
    QVERIFY(w.findChild<QWidget *>(s(u"navigatorPanel"))->isAncestorOf(first));
    QPointer<QWidget> watch(first);
    auto *second = new QWidget;
    w.setPanelWidget(MainWindow::Panel::Navigator, second); // replaces, and the old one is deleted
    QCOMPARE(w.panelWidget(MainWindow::Panel::Navigator), second);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(watch.isNull());
    w.setPanelWidget(MainWindow::Panel::Navigator, nullptr); // empties the panel
    QVERIFY(!w.panelWidget(MainWindow::Panel::Navigator));
    auto *right = new QWidget, *bar = new QWidget;
    w.setPanelWidget(MainWindow::Panel::Right, right);
    w.setPanelWidget(MainWindow::Panel::PlayerBar, bar);
    QVERIFY(w.findChild<QWidget *>(s(u"rightPanel"))->isAncestorOf(right));
    QVERIFY(w.findChild<QWidget *>(s(u"playerBar"))->isAncestorOf(bar));
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    QCOMPARE(bar->size(), w.findChild<QWidget *>(s(u"playerBar"))->size()); // a panel's widget fills it

    // W-PAGES
    QSignalSpy changed(&w, &MainWindow::currentPageChanged);
    QVERIFY(w.currentPage().isEmpty());
    QVERIFY(w.pages().isEmpty());
    QVERIFY(!w.showPage(s(u"tracks")));
    auto *tracks = new QWidget, *other = new QWidget;
    QVERIFY(w.addPage(s(u"tracks"), tracks));
    QCOMPARE(w.currentPage(), s(u"tracks")); // the first page added is shown
    QCOMPARE(changed.size(), 1);
    QVERIFY(w.addPage(s(u"other"), other));
    QCOMPARE(w.currentPage(), s(u"tracks"));
    QCOMPARE(w.pages(), QStringList({ s(u"tracks"), s(u"other") }));
    QWidget spare;
    QVERIFY(!w.addPage(s(u"tracks"), &spare)); // the id is taken
    QVERIFY(!w.addPage(QString(), &spare));
    QVERIFY(!w.addPage(s(u"none"), nullptr));
    QVERIFY(w.showPage(s(u"other")));
    QCOMPARE(w.currentPage(), s(u"other"));
    QCOMPARE(changed.size(), 2);
    QCOMPARE(changed.last().at(0).toString(), s(u"other"));
    QVERIFY(w.showPage(s(u"other"))); // already shown: fine, no signal
    QCOMPARE(changed.size(), 2);
    QVERIFY(!w.showPage(s(u"nope")));
    QCOMPARE(w.currentPage(), s(u"other"));
    auto *stack = w.findChild<QStackedWidget *>(s(u"mainPanel"));
    QCOMPARE(stack->currentWidget(), other);
    QVERIFY(other->isVisible() && !tracks->isVisible());
}

void MainWindowTest::viewActions()
{
    // W-COMMANDS
    Fixture f;
    MainWindow w(&f.actions, &f.settings);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    QWidget *navigator = w.findChild<QWidget *>(s(u"navigatorPanel"));
    QWidget *right = w.findChild<QWidget *>(s(u"rightPanel"));
    QAction *showNavigator = f.actions.action(s(u"view.navigator"));
    QAction *showRight = f.actions.action(s(u"view.rightPanel"));
    QAction *showStatus = f.actions.action(s(u"view.statusBar"));
    QVERIFY(navigator && right && showNavigator && showRight && showStatus);
    QVERIFY(f.actions.action(s(u"help.about")) && f.actions.action(s(u"view.fullScreen")) && f.actions.action(s(u"file.quit")));
    QVERIFY(showNavigator->isChecked() && showRight->isChecked() && showStatus->isChecked());
    QVERIFY(navigator->isVisible() && right->isVisible() && w.statusBar()->isVisible());
    const int before = w.findChild<QWidget *>(s(u"mainPanel"))->width();
    showNavigator->trigger();
    QVERIFY(!showNavigator->isChecked());
    QVERIFY(!navigator->isVisible());
    QVERIFY(right->isVisible());
    QTRY_VERIFY(w.findChild<QWidget *>(s(u"mainPanel"))->width() > before); // the middle takes the room
    showNavigator->trigger();
    QVERIFY(navigator->isVisible());
    showRight->trigger();
    showStatus->trigger();
    QVERIFY(!right->isVisible() && !w.statusBar()->isVisible());
    QVERIFY(w.findChild<QWidget *>(s(u"playerBar"))->isVisible()); // the player bar cannot be hidden
    // About: a dialog that does not block, saying which version this is.
    f.actions.action(s(u"help.about"))->trigger();
    QWidget *about = w.findChild<QWidget *>(s(u"aboutDialog"));
    QVERIFY(about);
    QTRY_VERIFY(about->isVisible());
    bool mentionsVersion = false;
    for (const QLabel *label : about->findChildren<QLabel *>())
        mentionsVersion = mentionsVersion || label->text().contains(s(u"linuxBee 0."));
    QVERIFY(mentionsVersion);
    about->close();
    // Full screen and back.
    QAction *full = f.actions.action(s(u"view.fullScreen"));
    full->trigger();
    QTRY_VERIFY(w.isFullScreen());
    full->trigger();
    QTRY_VERIFY(!w.isFullScreen());
    // Exit closes the window.
    f.actions.action(s(u"file.quit"))->trigger();
    QTRY_VERIFY(!w.isVisible());
}

void MainWindowTest::layoutIsRemembered()
{
    // W-LAYOUT
    QTemporaryDir dir;
    const QString file = dir.path() + s(u"/layout.ini");
    QList<int> sizes;
    {
        QSettings settings(file, QSettings::IniFormat);
        ActionRegistry actions;
        MainWindow w(&actions, &settings);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.resize(1000, 650);
        auto *splitter = w.findChild<QSplitter *>(s(u"mainSplitter"));
        QVERIFY(splitter);
        QVERIFY(actions.action(s(u"view.rightPanel")));
        splitter->setSizes({ 180, 500, 320 });
        QCoreApplication::processEvents();
        sizes = splitter->sizes();
        actions.action(s(u"view.rightPanel"))->trigger(); // hidden
        w.close(); // closing saves
    }
    {
        QSettings settings(file, QSettings::IniFormat);
        QVERIFY(settings.childGroups().contains(s(u"window")));
        ActionRegistry actions;
        MainWindow w(&actions, &settings);
        QVERIFY(actions.action(s(u"view.rightPanel")) && actions.action(s(u"view.navigator")));
        QCOMPARE(w.size(), QSize(1000, 650));
        QVERIFY(!actions.action(s(u"view.rightPanel"))->isChecked());
        QVERIFY(actions.action(s(u"view.navigator"))->isChecked());
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(w.findChild<QWidget *>(s(u"rightPanel")) && w.findChild<QSplitter *>(s(u"mainSplitter")));
        QVERIFY(!w.findChild<QWidget *>(s(u"rightPanel"))->isVisible());
        QVERIFY(w.findChild<QWidget *>(s(u"navigatorPanel"))->isVisible());
        QVERIFY(qAbs(w.findChild<QSplitter *>(s(u"mainSplitter"))->sizes().at(0) - sizes.at(0)) <= 2);
        // Showing the panel again gives it back the width it had.
        actions.action(s(u"view.rightPanel"))->trigger();
        QTRY_VERIFY(w.findChild<QWidget *>(s(u"rightPanel"))->isVisible());
        QTRY_VERIFY(qAbs(w.findChild<QWidget *>(s(u"rightPanel"))->width() - sizes.at(2)) <= 2);
        // saveLayout without closing; full screen is not saved as the size.
        w.resize(900, 600);
        actions.action(s(u"view.fullScreen"))->trigger();
        QTRY_VERIFY(w.isFullScreen());
        w.saveLayout();
    }
    {
        QSettings settings(file, QSettings::IniFormat);
        ActionRegistry actions;
        MainWindow w(&actions, &settings);
        QVERIFY(actions.action(s(u"view.rightPanel")));
        QCOMPARE(w.size(), QSize(900, 600));
        QVERIFY(!w.isFullScreen());
        QVERIFY(actions.action(s(u"view.rightPanel"))->isChecked());
    }
}

void MainWindowTest::layoutSurvivesNonsense()
{
    // W-LAYOUT: settings that cannot be right are ignored one by one.
    Fixture f;
    f.settings.setValue(s(u"window/size"), QSize(20, 99999));
    f.settings.setValue(s(u"window/splitter"), QVariantList({ -5, 100 }));
    f.settings.setValue(s(u"window/navigatorVisible"), s(u"maybe"));
    f.settings.setValue(s(u"window/rightPanelVisible"), false);
    MainWindow w(&f.actions, &f.settings);
    QCOMPARE(w.size(), QSize(1200, 760));
    QVERIFY(f.actions.action(s(u"view.rightPanel")));
    QVERIFY(!f.actions.action(s(u"view.rightPanel"))->isChecked()); // this one made sense
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    QVERIFY(w.findChild<QWidget *>(s(u"mainPanel")) && w.findChild<QWidget *>(s(u"navigatorPanel")));
    QVERIFY(w.findChild<QWidget *>(s(u"mainPanel"))->width() >= 200);
    QVERIFY(w.findChild<QWidget *>(s(u"navigatorPanel"))->width() >= 120);
    // Two windows on one settings file is not an error either.
    MainWindow second(&f.actions, &f.settings);
    QVERIFY(second.findChild<QSplitter *>(s(u"mainSplitter")));
}

QTEST_MAIN(MainWindowTest)
#include "tst_ui_mainwindow.moc"
