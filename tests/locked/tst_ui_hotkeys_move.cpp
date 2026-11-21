// LOCKED ACCEPTANCE TEST (a taken hotkey warns and moves). Written by the reviewer, not the
// builder. The builder makes these pass by changing src/, never by editing this file. A test
// believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/preferences.md (HK-CONFLICT, HK-MOVE, HK-BUTTONS).
// W-HK: MusicBee's "Hotkeys" page shows a warning icon when the typed key is assigned to another
// command, and asks for confirmation when Assign is clicked. The owner's ruling of 2026-11-20
// (N14): a taken hotkey warns and moves. Runs on the offscreen platform; no settings are written.
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QtTest>

#include "actions/actionregistry.h"
#include "preferences/hotkeyspage.h"

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

template <typename T> T *need(QWidget &w, const char16_t *name)
{
    if (T *found = w.findChild<T *>(s(name)))
        return found;
    QTest::qFail(qPrintable(QStringLiteral("no widget named ") + s(name)), __FILE__, __LINE__);
    return nullptr;
}

int rowOf(QTableWidget *table, const QString &id)
{
    for (int r = 0; r < table->rowCount(); ++r)
        if (table->item(r, 0) && table->item(r, 0)->data(Qt::UserRole).toString() == id)
            return r;
    return -1;
}

QString cell(QTableWidget *table, const char16_t *id)
{
    return table->item(rowOf(table, s(id)), 1)->text();
}

using Ok = ShortcutResult::Status;

}

class HotkeysMoveTest : public QObject {
    Q_OBJECT
private slots:
    void moves();
    void movesLikeAssign();
    void warnsWhileTyping();
    void assignAsksFirst();
};

void HotkeysMoveTest::moves()
{
    // HK-MOVE: the key is taken from the command that has it and given to this one, on the page.
    ActionRegistry actions;
    HotkeysPage page(&actions);
    auto *label = need<QLabel>(page, u"conflictLabel");
    auto *table = need<QTableWidget>(page, u"hotkeyTable");
    const ShortcutResult r = page.move(s(u"help.about"), key(u"Ctrl+R"));
    QVERIFY(r.status == Ok::Ok);
    QCOMPARE(r.conflictsWith, s(u"file.organise")); // who lost it
    QCOMPARE(page.pending(s(u"help.about")), key(u"Ctrl+R"));
    QCOMPARE(page.pending(s(u"file.organise")), QKeySequence());
    QCOMPARE(cell(table, u"help.about"), shown(u"Ctrl+R"));
    QCOMPARE(cell(table, u"file.organise"), QString());
    QCOMPARE(label->text(), shown(u"Ctrl+R") + s(u" moved from: File > Organise Files"));
    QVERIFY(page.isModified());
    QCOMPARE(actions.shortcut(s(u"file.organise")), key(u"Ctrl+R")); // nothing applied yet
    QCOMPARE(page.apply(), QString());
    QCOMPARE(actions.shortcut(s(u"help.about")), key(u"Ctrl+R"));
    QCOMPARE(actions.shortcut(s(u"file.organise")), QKeySequence());
}

void HotkeysMoveTest::movesLikeAssign()
{
    // HK-MOVE: with nobody else holding the key, move is assign.
    ActionRegistry actions;
    HotkeysPage page(&actions);
    auto *label = need<QLabel>(page, u"conflictLabel");
    const ShortcutResult free = page.move(s(u"help.about"), key(u"F1"));
    QVERIFY(free.status == Ok::Ok);
    QCOMPARE(free.conflictsWith, QString());
    QCOMPARE(page.pending(s(u"help.about")), key(u"F1"));
    QCOMPARE(label->text(), QString());
    const ShortcutResult own = page.move(s(u"file.organise"), key(u"Ctrl+R"));
    QVERIFY(own.status == Ok::Ok);
    QCOMPARE(own.conflictsWith, QString());
    QCOMPARE(page.pending(s(u"file.organise")), key(u"Ctrl+R"));
    QVERIFY(page.move(s(u"file.organise"), QKeySequence()).status == Ok::Ok);
    QCOMPARE(page.pending(s(u"file.organise")), QKeySequence());
    QVERIFY(page.move(s(u"nope"), key(u"F2")).status == Ok::UnknownAction);
    QVERIFY(page.move(s(u"help.about"), key(u"Ctrl+K, Ctrl+L")).status == Ok::Invalid);
    QCOMPARE(label->text(), s(u"Only one key combination can be used."));
    QCOMPARE(page.pending(s(u"help.about")), key(u"F1"));
    // assign() still refuses a taken key (HK-CONFLICT).
    const ShortcutResult refused = page.assign(s(u"help.about"), key(u"Ctrl+Q"));
    QVERIFY(refused.status == Ok::Conflict);
    QCOMPARE(refused.conflictsWith, s(u"file.quit"));
    QCOMPARE(page.pending(s(u"file.quit")), key(u"Ctrl+Q"));
}

void HotkeysMoveTest::warnsWhileTyping()
{
    // HK-BUTTONS: while the key in the field is another command's pending key, the warning icon
    // shows and the label says who has it. Nothing changes until Assign.
    ActionRegistry actions;
    HotkeysPage page(&actions);
    auto *table = need<QTableWidget>(page, u"hotkeyTable");
    auto *edit = need<QKeySequenceEdit>(page, u"keyEdit");
    auto *label = need<QLabel>(page, u"conflictLabel");
    auto *icon = need<QLabel>(page, u"conflictIcon");
    QVERIFY(icon->isHidden());
    table->setCurrentCell(rowOf(table, s(u"help.about")), 0);
    edit->setKeySequence(key(u"Ctrl+R"));
    QVERIFY(!icon->isHidden());
    QCOMPARE(label->text(), shown(u"Ctrl+R") + s(u" is used by: File > Organise Files"));
    QCOMPARE(page.pending(s(u"help.about")), QKeySequence());
    QVERIFY(!page.isModified());
    edit->setKeySequence(key(u"F1")); // a free key: the warning goes
    QVERIFY(icon->isHidden());
    QCOMPARE(label->text(), QString());
    table->setCurrentCell(rowOf(table, s(u"file.organise")), 0); // its own key is no warning
    QCOMPARE(edit->keySequence(), key(u"Ctrl+R"));
    QVERIFY(icon->isHidden());
    // Judged against the pending keys: freed on the page, no warning.
    QVERIFY(page.assign(s(u"file.organise"), QKeySequence()).status == Ok::Ok);
    table->setCurrentCell(rowOf(table, s(u"help.about")), 0);
    edit->setKeySequence(key(u"Ctrl+R"));
    QVERIFY(icon->isHidden());
    edit->setKeySequence(key(u"Ctrl+Q"));
    QVERIFY(!icon->isHidden());
    page.load(); // load() forgets the warning with the edits
    QVERIFY(icon->isHidden());
    QCOMPARE(label->text(), QString());
}

void HotkeysMoveTest::assignAsksFirst()
{
    // HK-BUTTONS: Assign with a taken key asks first; yes moves it, no changes nothing.
    ActionRegistry actions;
    HotkeysPage page(&actions);
    QList<QPair<QKeySequence, QString>> asked;
    bool answer = false;
    page.setConfirmMove([&](const QKeySequence &taken, const QString &holder) {
        asked.append({ taken, holder });
        return answer;
    });
    auto *table = need<QTableWidget>(page, u"hotkeyTable");
    auto *edit = need<QKeySequenceEdit>(page, u"keyEdit");
    auto *assign = need<QPushButton>(page, u"assignButton");
    auto *label = need<QLabel>(page, u"conflictLabel");
    auto *icon = need<QLabel>(page, u"conflictIcon");
    table->setCurrentCell(rowOf(table, s(u"help.about")), 0);
    edit->setKeySequence(key(u"Ctrl+R"));
    assign->click();
    QCOMPARE(asked.size(), 1);
    QCOMPARE(asked.at(0).first, key(u"Ctrl+R"));
    QCOMPARE(asked.at(0).second, s(u"file.organise"));
    QCOMPARE(page.pending(s(u"help.about")), QKeySequence()); // no: nothing changed
    QCOMPARE(page.pending(s(u"file.organise")), key(u"Ctrl+R"));
    QCOMPARE(label->text(), shown(u"Ctrl+R") + s(u" is used by: File > Organise Files"));
    QVERIFY(!page.isModified());
    answer = true;
    assign->click();
    QCOMPARE(asked.size(), 2);
    QCOMPARE(page.pending(s(u"help.about")), key(u"Ctrl+R")); // yes: moved
    QCOMPARE(page.pending(s(u"file.organise")), QKeySequence());
    QCOMPARE(label->text(), shown(u"Ctrl+R") + s(u" moved from: File > Organise Files"));
    QVERIFY(icon->isHidden());
    edit->setKeySequence(key(u"F1")); // a free key is not asked about
    assign->click();
    QCOMPARE(asked.size(), 2);
    QCOMPARE(page.pending(s(u"help.about")), key(u"F1"));
    need<QPushButton>(page, u"clearButton")->click(); // nor is Clear
    QCOMPARE(asked.size(), 2);
    QCOMPARE(page.pending(s(u"help.about")), QKeySequence());
}

QTEST_MAIN(HotkeysMoveTest)
#include "tst_ui_hotkeys_move.moc"
