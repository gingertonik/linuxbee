// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/preferences.md (PF-x, HK-x). Runs on
// the offscreen platform.
#include <QAction>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtTest>

#include "actions/actionregistry.h"
#include "preferences/hotkeyspage.h"
#include "preferences/preferencesdialog.h"

using namespace linuxbee::ui;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

QKeySequence key(const char16_t *portable)
{
    return QKeySequence(s(portable), QKeySequence::PortableText);
}

QString shown(const char16_t *portable)
{
    return key(portable).toString(QKeySequence::NativeText);
}

// The named child, or (failing the test) a stand-in, so that a missing widget
// is a failure and not a crash.
template <typename T> T *need(QWidget &w, const char16_t *name)
{
    if (T *found = w.findChild<T *>(s(name)))
        return found;
    QTest::qFail(qPrintable(QStringLiteral("no widget named ") + s(name)), __FILE__, __LINE__);
    static T standIn;
    return &standIn;
}

// A page that writes down what is asked of it.
class Recorder : public PreferencesPage {
    Q_OBJECT
public:
    QString name;
    QString refusal; // what apply() answers
    QStringList *log;
    bool modified = false;
    Recorder(const QString &n, QStringList *l)
        : name(n)
        , log(l)
    {
    }
    QString title() const override { return name; }
    void load() override
    {
        log->append(name + s(u" load"));
        touch(false);
    }
    QString apply() override
    {
        log->append(name + s(u" apply"));
        if (refusal.isEmpty())
            touch(false);
        return refusal;
    }
    bool isModified() const override { return modified; }
    void touch(bool now)
    {
        if (modified == now)
            return;
        modified = now;
        emit modifiedChanged();
    }
};

QStringList take(QStringList &log)
{
    const QStringList out = log;
    log.clear();
    return out;
}

int rowOf(QTableWidget *table, const QString &id)
{
    for (int r = 0; r < table->rowCount(); ++r)
        if (table->item(r, 0) && table->item(r, 0)->data(Qt::UserRole).toString() == id)
            return r;
    return -1;
}

QStringList visibleIds(QTableWidget *table)
{
    QStringList out;
    for (int r = 0; r < table->rowCount(); ++r)
        if (!table->isRowHidden(r) && table->item(r, 0))
            out.append(table->item(r, 0)->data(Qt::UserRole).toString());
    return out;
}

}

class PreferencesTest : public QObject {
    Q_OBJECT
private slots:
    void dialogStructure();
    void pages();
    void applying();
    void refusal();
    void saveAndCancel();
    void hotkeyTable();
    void hotkeyFilter();
    void hotkeyEditing();
    void hotkeyConflicts();
    void hotkeyReset();
    void hotkeyApply();
    void hotkeyButtons();
    void together();
};

void PreferencesTest::dialogStructure()
{
    // PF-FRAME
    PreferencesDialog d;
    QCOMPARE(d.windowTitle(), s(u"Preferences"));
    QVERIFY(d.findChild<QListWidget *>(s(u"pageList")));
    QVERIFY(d.findChild<QStackedWidget *>(s(u"pageStack")));
    QVERIFY(d.findChild<QLabel *>(s(u"errorLabel")));
    for (const char16_t *n : { u"saveButton", u"applyButton", u"cancelButton" })
        QVERIFY2(d.findChild<QPushButton *>(s(n)), qPrintable(s(n)));
    QCOMPARE(d.pages(), QStringList());
    QCOMPARE(d.currentPage(), QString());
    QVERIFY(!need<QPushButton>(d, u"applyButton")->isEnabled());
    QVERIFY(need<QPushButton>(d, u"saveButton")->isEnabled());
    QCOMPARE(need<QLabel>(d, u"errorLabel")->text(), QString());
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));
    auto *list = need<QListWidget>(d, u"pageList");
    auto *stack = need<QStackedWidget>(d, u"pageStack");
    QVERIFY(list->mapTo(&d, QPoint(0, 0)).x() < stack->mapTo(&d, QPoint(0, 0)).x()); // the list is on the left
    QVERIFY(stack->width() > list->width());
}

void PreferencesTest::pages()
{
    // PF-PAGES
    QStringList log;
    PreferencesDialog d;
    auto *a = new Recorder(s(u"General"), &log);
    auto *b = new Recorder(s(u"Tags"), &log);
    d.addPage(a);
    QCOMPARE(take(log), QStringList({ s(u"General load") })); // a page is loaded when it is added
    d.addPage(b);
    QCOMPARE(d.pages(), QStringList({ s(u"General"), s(u"Tags") }));
    auto *list = need<QListWidget>(d, u"pageList");
    auto *stack = need<QStackedWidget>(d, u"pageStack");
    QCOMPARE(list->count(), 2);
    QCOMPARE(list->item(1)->text(), s(u"Tags"));
    QCOMPARE(d.currentPage(), s(u"General")); // the first page added is shown
    QCOMPARE(stack->currentWidget(), a);
    QVERIFY(d.selectPage(s(u"Tags")));
    QCOMPARE(d.currentPage(), s(u"Tags"));
    QCOMPARE(stack->currentWidget(), b);
    QCOMPARE(list->currentRow(), 1);
    QVERIFY(!d.selectPage(s(u"tags")));
    QVERIFY(!d.selectPage(s(u"Nope")));
    QCOMPARE(d.currentPage(), s(u"Tags"));
    list->setCurrentRow(0); // choosing in the list changes the page
    QCOMPARE(stack->currentWidget(), a);
    QCOMPARE(d.currentPage(), s(u"General"));
    // The dialog owns its pages.
    QPointer<Recorder> watch(b);
    {
        PreferencesDialog other;
        other.addPage(new Recorder(s(u"X"), &log));
    }
    QVERIFY(watch);
}

void PreferencesTest::applying()
{
    // PF-APPLY
    QStringList log;
    PreferencesDialog d;
    auto *a = new Recorder(s(u"General"), &log);
    auto *b = new Recorder(s(u"Tags"), &log);
    auto *c = new Recorder(s(u"Player"), &log);
    d.addPage(a);
    d.addPage(b);
    d.addPage(c);
    take(log);
    QSignalSpy applied(&d, &PreferencesDialog::applied);
    auto *apply = need<QPushButton>(d, u"applyButton");
    QVERIFY(!apply->isEnabled());
    QVERIFY(d.apply()); // nothing to do is not a failure
    QCOMPARE(take(log), QStringList());
    QCOMPARE(applied.size(), 0);
    c->touch(true);
    QVERIFY(apply->isEnabled());
    a->touch(true);
    apply->click();
    QCOMPARE(take(log), QStringList({ s(u"General apply"), s(u"Player apply") })); // only the changed pages, in order
    QCOMPARE(applied.size(), 1);
    QVERIFY(!apply->isEnabled());
    QVERIFY(d.result() != QDialog::Accepted); // Apply leaves the dialog open
    b->touch(true);
    b->touch(false); // changed back
    QVERIFY(!apply->isEnabled());
}

void PreferencesTest::refusal()
{
    // PF-APPLY: a page that refuses stops the run and is shown.
    QStringList log;
    PreferencesDialog d;
    auto *a = new Recorder(s(u"General"), &log);
    auto *b = new Recorder(s(u"Tags"), &log);
    auto *c = new Recorder(s(u"Player"), &log);
    d.addPage(a);
    d.addPage(b);
    d.addPage(c);
    take(log);
    QSignalSpy applied(&d, &PreferencesDialog::applied);
    QSignalSpy accepted(&d, &QDialog::accepted);
    a->touch(true);
    b->touch(true);
    c->touch(true);
    b->refusal = s(u"the folder does not exist");
    need<QPushButton>(d, u"saveButton")->click();
    QCOMPARE(take(log), QStringList({ s(u"General apply"), s(u"Tags apply") })); // Player is not tried
    QCOMPARE(need<QLabel>(d, u"errorLabel")->text(), s(u"Tags: the folder does not exist"));
    QCOMPARE(d.currentPage(), s(u"Tags"));
    QCOMPARE(accepted.size(), 0); // Save does not close
    QCOMPARE(applied.size(), 1); // General was stored
    QVERIFY(need<QPushButton>(d, u"applyButton")->isEnabled());
    QVERIFY(!d.apply());
    QCOMPARE(take(log), QStringList({ s(u"Tags apply") })); // General is done already
    b->refusal.clear();
    QVERIFY(d.apply());
    QCOMPARE(take(log), QStringList({ s(u"Tags apply"), s(u"Player apply") }));
    QCOMPARE(need<QLabel>(d, u"errorLabel")->text(), QString()); // the message goes with the next try
}

void PreferencesTest::saveAndCancel()
{
    // PF-APPLY, PF-CANCEL
    QStringList log;
    PreferencesDialog d;
    auto *a = new Recorder(s(u"General"), &log);
    auto *b = new Recorder(s(u"Tags"), &log);
    d.addPage(a);
    d.addPage(b);
    take(log);
    QSignalSpy accepted(&d, &QDialog::accepted);
    QSignalSpy rejected(&d, &QDialog::rejected);
    QSignalSpy applied(&d, &PreferencesDialog::applied);
    b->touch(true);
    need<QPushButton>(d, u"saveButton")->click();
    QCOMPARE(take(log), QStringList({ s(u"Tags apply") }));
    QCOMPARE(accepted.size(), 1);
    QCOMPARE(applied.size(), 1);
    need<QPushButton>(d, u"saveButton")->click(); // nothing changed: Save still closes
    QCOMPARE(accepted.size(), 2);
    QCOMPARE(applied.size(), 1);
    QCOMPARE(take(log), QStringList());
    // Cancel: every page forgets its edits, nothing is applied.
    a->touch(true);
    b->refusal = s(u"old message");
    b->touch(true);
    d.apply();
    b->refusal.clear();
    take(log);
    need<QPushButton>(d, u"cancelButton")->click();
    QCOMPARE(take(log), QStringList({ s(u"General load"), s(u"Tags load") }));
    QCOMPARE(rejected.size(), 1);
    QVERIFY(!need<QPushButton>(d, u"applyButton")->isEnabled());
    QCOMPARE(need<QLabel>(d, u"errorLabel")->text(), QString());
    // Escape, or closing the window, is Cancel.
    a->touch(true);
    d.reject();
    QCOMPARE(take(log), QStringList({ s(u"General load"), s(u"Tags load") }));
    QVERIFY(!a->isModified());
}

void PreferencesTest::hotkeyTable()
{
    // HK-TABLE
    ActionRegistry actions;
    HotkeysPage page(&actions);
    QCOMPARE(page.title(), s(u"Hotkeys"));
    QVERIFY(page.findChild<QLineEdit *>(s(u"filterEdit")));
    QVERIFY(page.findChild<QKeySequenceEdit *>(s(u"keyEdit")));
    QVERIFY(page.findChild<QLabel *>(s(u"conflictLabel")));
    for (const char16_t *n : { u"assignButton", u"clearButton", u"resetButton", u"resetAllButton" })
        QVERIFY2(page.findChild<QPushButton *>(s(n)), qPrintable(s(n)));
    auto *table = need<QTableWidget>(page, u"hotkeyTable");
    QCOMPARE(table->columnCount(), 2);
    QCOMPARE(table->horizontalHeaderItem(0)->text(), s(u"Command"));
    QCOMPARE(table->horizontalHeaderItem(1)->text(), s(u"Hotkey"));
    QVERIFY(table->selectionBehavior() == QAbstractItemView::SelectRows);
    QVERIFY(table->selectionMode() == QAbstractItemView::SingleSelection);
    QVERIFY(table->editTriggers() == QAbstractItemView::NoEditTriggers);
    // One row per command, in catalogue order.
    QCOMPARE(visibleIds(table), actions.ids());
    QVERIFY(actions.ids().size() > 60);
    auto cell = [&](const char16_t *id, int column) {
        const int row = rowOf(table, s(id));
        return row < 0 || !table->item(row, column) ? s(u"NO ROW") : table->item(row, column)->text();
    };
    // The command is named with its menu, without the dots and the ampersands.
    QCOMPARE(cell(u"file.organise", 0), s(u"File > Organise Files"));
    QCOMPARE(cell(u"file.playlists.new", 0), s(u"File > Playlists > New Playlist"));
    QCOMPARE(cell(u"tools.tagging.capitalise", 0), s(u"Tools > Tagging Tools > Capitalise"));
    QCOMPARE(cell(u"track.rate.3", 0), s(u"Context > Rating > 3 Stars"));
    QCOMPARE(cell(u"playing.rate.0", 0), s(u"Playing Track: 0 Stars")); // a command in no menu
    QCOMPARE(cell(u"file.organise", 1), shown(u"Ctrl+R"));
    QCOMPARE(cell(u"controls.volumeUp", 1), shown(u"Ctrl+Alt+Up"));
    QCOMPARE(cell(u"help.about", 1), QString());
    QVERIFY(!page.isModified());
    QCOMPARE(page.pending(s(u"file.organise")), key(u"Ctrl+R"));
    QCOMPARE(page.pending(s(u"nope")), QKeySequence());
    // Nothing chosen yet: the three row buttons wait.
    QVERIFY(!need<QPushButton>(page, u"assignButton")->isEnabled());
    QVERIFY(!need<QPushButton>(page, u"clearButton")->isEnabled());
    QVERIFY(!need<QPushButton>(page, u"resetButton")->isEnabled());
    QVERIFY(need<QPushButton>(page, u"resetAllButton")->isEnabled());
}

void PreferencesTest::hotkeyFilter()
{
    // HK-FILTER
    ActionRegistry actions;
    HotkeysPage page(&actions);
    auto *table = need<QTableWidget>(page, u"hotkeyTable");
    auto *filter = need<QLineEdit>(page, u"filterEdit");
    filter->setText(s(u"volume"));
    QCOMPARE(visibleIds(table),
        QStringList({ s(u"controls.volumeUp"), s(u"controls.volumeDown"), s(u"tools.analyseVolume"), s(u"tools.restoreVolume") }));
    filter->setText(s(u"  TAGGING TOOLS > re "));
    QCOMPARE(visibleIds(table),
        QStringList({ s(u"tools.tagging.removeTags"), s(u"tools.tagging.renumber"), s(u"tools.tagging.resetCounts") }));
    filter->setText(shown(u"Ctrl+Alt+V").toLower()); // keys are searched as shown
    QCOMPARE(visibleIds(table), QStringList({ s(u"controls.mute") }));
    filter->setText(s(u"no such thing"));
    QCOMPARE(visibleIds(table), QStringList());
    filter->clear();
    QCOMPARE(visibleIds(table), actions.ids());
    // The filter sees edits not yet applied.
    QVERIFY(page.assign(s(u"help.about"), key(u"Ctrl+F9")).status == ShortcutResult::Status::Ok);
    filter->setText(s(u"f9"));
    QCOMPARE(visibleIds(table), QStringList({ s(u"help.about") }));
}

void PreferencesTest::hotkeyEditing()
{
    // HK-EDIT: edits stay on the page.
    ActionRegistry actions;
    HotkeysPage page(&actions);
    auto *table = need<QTableWidget>(page, u"hotkeyTable");
    QSignalSpy modified(&page, &PreferencesPage::modifiedChanged);
    QSignalSpy changed(&actions, &ActionRegistry::shortcutChanged);
    QVERIFY(page.assign(s(u"help.about"), key(u"F1")).status == ShortcutResult::Status::Ok);
    QCOMPARE(page.pending(s(u"help.about")), key(u"F1"));
    QCOMPARE(table->item(rowOf(table, s(u"help.about")), 1)->text(), shown(u"F1"));
    QVERIFY(page.isModified());
    QCOMPARE(modified.size(), 1);
    QCOMPARE(actions.shortcut(s(u"help.about")), QKeySequence()); // not applied
    QCOMPARE(changed.size(), 0);
    QVERIFY(page.assign(s(u"help.about"), key(u"F1")).status == ShortcutResult::Status::Ok); // the same again
    QVERIFY(page.assign(s(u"file.quit"), QKeySequence()).status == ShortcutResult::Status::Ok); // no key at all
    QCOMPARE(page.pending(s(u"file.quit")), QKeySequence());
    QCOMPARE(modified.size(), 1); // still modified: said once
    // Putting everything back is "not modified" again.
    QVERIFY(page.assign(s(u"help.about"), QKeySequence()).status == ShortcutResult::Status::Ok);
    QVERIFY(page.isModified());
    QVERIFY(page.assign(s(u"file.quit"), key(u"Ctrl+Q")).status == ShortcutResult::Status::Ok);
    QVERIFY(!page.isModified());
    QCOMPARE(modified.size(), 2);
    // Refused without changing anything.
    QVERIFY(page.assign(s(u"nope"), key(u"F1")).status == ShortcutResult::Status::UnknownAction);
    QVERIFY(page.assign(s(u"help.about"), key(u"Ctrl+K, Ctrl+L")).status == ShortcutResult::Status::Invalid);
    QCOMPARE(page.pending(s(u"help.about")), QKeySequence());
    QVERIFY(!page.isModified());
    // load() forgets edits.
    page.assign(s(u"help.about"), key(u"F1"));
    page.load();
    QCOMPARE(page.pending(s(u"help.about")), QKeySequence());
    QCOMPARE(table->item(rowOf(table, s(u"help.about")), 1)->text(), QString());
    QVERIFY(!page.isModified());
    QCOMPARE(modified.size(), 4);
}

void PreferencesTest::hotkeyConflicts()
{
    // HK-CONFLICT: judged against the edits on the page, not against what is stored.
    ActionRegistry actions;
    HotkeysPage page(&actions);
    auto *label = need<QLabel>(page, u"conflictLabel");
    QCOMPARE(label->text(), QString());
    const ShortcutResult taken = page.assign(s(u"help.about"), key(u"Ctrl+R"));
    QVERIFY(taken.status == ShortcutResult::Status::Conflict);
    QCOMPARE(taken.conflictsWith, s(u"file.organise"));
    QCOMPARE(label->text(), shown(u"Ctrl+R") + s(u" is used by: File > Organise Files"));
    QCOMPARE(page.pending(s(u"help.about")), QKeySequence());
    QVERIFY(!page.isModified());
    // Free the key on the page, and it can be given away, although the registry still has it.
    QVERIFY(page.assign(s(u"file.organise"), QKeySequence()).status == ShortcutResult::Status::Ok);
    QCOMPARE(label->text(), QString()); // a good edit clears the message
    QVERIFY(page.assign(s(u"help.about"), key(u"Ctrl+R")).status == ShortcutResult::Status::Ok);
    // And a key only pending is in the way too.
    const ShortcutResult again = page.assign(s(u"file.organise"), key(u"Ctrl+R"));
    QVERIFY(again.status == ShortcutResult::Status::Conflict);
    QCOMPARE(again.conflictsWith, s(u"help.about"));
    QCOMPARE(label->text(), shown(u"Ctrl+R") + s(u" is used by: Help > About linuxBee"));
    QVERIFY(page.assign(s(u"help.about"), key(u"Ctrl+K, Ctrl+L")).status == ShortcutResult::Status::Invalid);
    QCOMPARE(label->text(), s(u"Only one key combination can be used."));
}

void PreferencesTest::hotkeyReset()
{
    // HK-RESET
    ActionRegistry actions;
    QVERIFY(actions.setShortcut(s(u"file.organise"), key(u"F7")).status == ShortcutResult::Status::Ok);
    HotkeysPage page(&actions);
    QCOMPARE(page.pending(s(u"file.organise")), key(u"F7"));
    page.reset(s(u"file.organise"));
    QCOMPARE(page.pending(s(u"file.organise")), key(u"Ctrl+R"));
    QVERIFY(page.isModified());
    page.reset(s(u"nope")); // nothing happens
    // A default that another command has on the page: no key, and the label says who.
    page.load();
    QVERIFY(page.assign(s(u"help.about"), key(u"Ctrl+R")).status == ShortcutResult::Status::Ok);
    page.reset(s(u"file.organise"));
    QCOMPARE(page.pending(s(u"file.organise")), QKeySequence());
    QCOMPARE(need<QLabel>(page, u"conflictLabel")->text(), shown(u"Ctrl+R") + s(u" is used by: Help > About linuxBee"));
    // All defaults: every command, whatever was pending.
    page.resetAll();
    for (const QString &id : actions.ids())
        QVERIFY2(page.pending(id) == actions.defaultShortcut(id), qPrintable(id));
    QCOMPARE(need<QLabel>(page, u"conflictLabel")->text(), QString());
    QVERIFY(page.isModified()); // the registry still has F7
    QCOMPARE(actions.shortcut(s(u"file.organise")), key(u"F7"));
}

void PreferencesTest::hotkeyApply()
{
    // HK-APPLY
    QTemporaryDir dir;
    QSettings settings(dir.path() + s(u"/k.ini"), QSettings::IniFormat);
    ActionRegistry actions;
    HotkeysPage page(&actions, &settings);
    // Two commands swap keys, a third takes one that is freed, a fourth loses its key.
    QVERIFY(page.assign(s(u"controls.stop"), QKeySequence()).status == ShortcutResult::Status::Ok);
    QVERIFY(page.assign(s(u"controls.next"), key(u"Ctrl+S")).status == ShortcutResult::Status::Ok);
    QVERIFY(page.assign(s(u"controls.stop"), key(u"Ctrl+N")).status == ShortcutResult::Status::Ok);
    QVERIFY(page.assign(s(u"file.quit"), QKeySequence()).status == ShortcutResult::Status::Ok);
    QVERIFY(page.assign(s(u"help.about"), key(u"Ctrl+Q")).status == ShortcutResult::Status::Ok);
    QSignalSpy modified(&page, &PreferencesPage::modifiedChanged);
    QCOMPARE(page.apply(), QString());
    QCOMPARE(actions.shortcut(s(u"controls.stop")), key(u"Ctrl+N"));
    QCOMPARE(actions.shortcut(s(u"controls.next")), key(u"Ctrl+S"));
    QCOMPARE(actions.shortcut(s(u"file.quit")), QKeySequence());
    QCOMPARE(actions.shortcut(s(u"help.about")), key(u"Ctrl+Q"));
    QCOMPARE(actions.action(s(u"help.about"))->shortcut(), key(u"Ctrl+Q"));
    QCOMPARE(actions.shortcut(s(u"file.organise")), key(u"Ctrl+R")); // untouched
    QVERIFY(!page.isModified());
    QCOMPARE(modified.size(), 1);
    // Stored too, so that the next start has them.
    settings.sync();
    ActionRegistry fresh;
    fresh.load(settings);
    QCOMPARE(fresh.shortcut(s(u"controls.stop")), key(u"Ctrl+N"));
    QCOMPARE(fresh.shortcut(s(u"controls.next")), key(u"Ctrl+S"));
    QCOMPARE(fresh.shortcut(s(u"file.quit")), QKeySequence());
    QCOMPARE(fresh.shortcut(s(u"help.about")), key(u"Ctrl+Q"));
    // Without a settings object the registry is still changed.
    HotkeysPage plain(&actions);
    plain.assign(s(u"view.refresh"), key(u"F6"));
    QCOMPARE(plain.apply(), QString());
    QCOMPARE(actions.shortcut(s(u"view.refresh")), key(u"F6"));
    // Applying with nothing changed changes nothing.
    QSignalSpy changed(&actions, &ActionRegistry::shortcutChanged);
    QCOMPARE(plain.apply(), QString());
    QCOMPARE(changed.size(), 0);
}

void PreferencesTest::hotkeyButtons()
{
    // HK-BUTTONS: the widgets drive the same four functions.
    ActionRegistry actions;
    HotkeysPage page(&actions);
    auto *table = need<QTableWidget>(page, u"hotkeyTable");
    auto *edit = need<QKeySequenceEdit>(page, u"keyEdit");
    auto *assign = need<QPushButton>(page, u"assignButton");
    table->setCurrentCell(rowOf(table, s(u"file.organise")), 0);
    QVERIFY(assign->isEnabled());
    QVERIFY(need<QPushButton>(page, u"clearButton")->isEnabled());
    QCOMPARE(edit->keySequence(), key(u"Ctrl+R")); // choosing a row shows its key
    edit->setKeySequence(key(u"F8"));
    assign->click();
    QCOMPARE(page.pending(s(u"file.organise")), key(u"F8"));
    edit->setKeySequence(key(u"Ctrl+Q"));
    assign->click();
    QCOMPARE(page.pending(s(u"file.organise")), key(u"F8")); // refused
    QCOMPARE(need<QLabel>(page, u"conflictLabel")->text(), shown(u"Ctrl+Q") + s(u" is used by: File > Exit"));
    table->setCurrentCell(rowOf(table, s(u"file.quit")), 0); // another row: its key, and the message goes
    QCOMPARE(edit->keySequence(), key(u"Ctrl+Q"));
    QCOMPARE(need<QLabel>(page, u"conflictLabel")->text(), QString());
    need<QPushButton>(page, u"clearButton")->click();
    QCOMPARE(page.pending(s(u"file.quit")), QKeySequence());
    QCOMPARE(edit->keySequence(), QKeySequence());
    need<QPushButton>(page, u"resetButton")->click();
    QCOMPARE(page.pending(s(u"file.quit")), key(u"Ctrl+Q"));
    QCOMPARE(edit->keySequence(), key(u"Ctrl+Q"));
    need<QPushButton>(page, u"resetAllButton")->click();
    QCOMPARE(page.pending(s(u"file.organise")), key(u"Ctrl+R"));
    QVERIFY(!page.isModified());
    // The key field takes one combination only.
    QCOMPARE(edit->maximumSequenceLength(), qsizetype(1));
}

void PreferencesTest::together()
{
    // The page inside the dialog.
    ActionRegistry actions;
    PreferencesDialog d;
    auto *page = new HotkeysPage(&actions);
    d.addPage(page);
    QCOMPARE(d.pages(), QStringList({ s(u"Hotkeys") }));
    auto *apply = need<QPushButton>(d, u"applyButton");
    QVERIFY(!apply->isEnabled());
    page->assign(s(u"help.about"), key(u"F1"));
    QVERIFY(apply->isEnabled());
    need<QPushButton>(d, u"cancelButton")->click();
    QCOMPARE(page->pending(s(u"help.about")), QKeySequence());
    QCOMPARE(actions.shortcut(s(u"help.about")), QKeySequence());
    page->assign(s(u"help.about"), key(u"F1"));
    QSignalSpy applied(&d, &PreferencesDialog::applied);
    need<QPushButton>(d, u"saveButton")->click();
    QCOMPARE(actions.shortcut(s(u"help.about")), key(u"F1"));
    QCOMPARE(applied.size(), 1);
    QVERIFY(d.result() == QDialog::Accepted);
}

QTEST_MAIN(PreferencesTest)
#include "tst_ui_preferences.moc"
