// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/tag-editor.md (E-DIALOG). Runs on
// the offscreen platform. The last test writes a real file, a copy of a
// plan-03 fixture in a temporary folder.
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "tagedit/tagedit.h"
#include "tageditor/tageditordialog.h"

using namespace linuxbee;
using linuxbee::ui::EditTarget;
using linuxbee::ui::TagEditorDialog;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

EditTarget target(qint64 id, const char16_t *title, const char16_t *year)
{
    EditTarget t;
    t.id = id;
    t.path = QStringLiteral("/music/%1 - %2.flac").arg(id).arg(s(title));
    t.tags.fields.insert(s(u"Title"), { s(title) });
    t.tags.fields.insert(s(u"Artist"), { s(u"Bj\u00f6rk") });
    t.tags.fields.insert(s(u"Album"), { s(u"Homogenic") });
    t.tags.fields.insert(s(u"Genre"), { s(u"Electronic"), s(u"Trip Hop") });
    if (*year)
        t.tags.fields.insert(s(u"Year"), { s(year) });
    return t;
}

QList<EditTarget> three()
{
    return { target(11, u"Hunter", u"1997"), target(12, u"J\u00f3ga", u""), target(13, u"Unravel", u"1997") };
}

// The named child, or (failing the test) a stand-in, so that a missing widget
// is a failure and not a crash.
template <typename T> T *need(QWidget &d, const QString &name)
{
    if (T *found = d.findChild<T *>(name))
        return found;
    QTest::qFail(qPrintable(QStringLiteral("no widget named ") + name), __FILE__, __LINE__);
    static T standIn;
    return &standIn;
}

QLineEdit *editor(TagEditorDialog &d, const char16_t *field)
{
    return need<QLineEdit>(d, s(u"field.") + s(field));
}

struct Recorder {
    QStringList paths;
    QList<TagChanges> changes;
    QStringList failFor; // paths that cannot be saved
    QStringList unchangedFor;
    TagEditorDialog::Saver saver()
    {
        return [this](const QString &path, const TagChanges &c) {
            paths.append(path);
            changes.append(c);
            WriteResult r;
            r.status = failFor.contains(path) ? WriteResult::Status::NotWritable
                                              : (unchangedFor.contains(path) ? WriteResult::Status::Unchanged : WriteResult::Status::Ok);
            return r;
        };
    }
};

void type(QLineEdit *edit, const QString &text)
{
    edit->setFocus();
    edit->selectAll();
    QTest::keyClicks(edit, text);
}

}

class TagEditorTest : public QObject {
    Q_OBJECT
private slots:
    void structure();
    void oneTrack();
    void manyTracks();
    void mixedFieldCanBeCleared();
    void refusedEdit();
    void failedSave();
    void editAfterFailedSave();
    void cancel();
    void noTargets();
    void realFile();
};

void TagEditorTest::structure()
{
    TagEditorDialog d({ target(11, u"Hunter", u"1997") });
    QCOMPARE(d.objectName(), s(u"tagEditorDialog"));
    QVERIFY(!d.windowTitle().isEmpty());
    // One line editor per E-FIELDS field, in that order from the top.
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));
    int lastY = -1;
    for (const QString &field : tagEditorFields()) {
        auto *edit = d.findChild<QLineEdit *>(s(u"field.") + field);
        QVERIFY2(edit, qPrintable(field));
        const int y = edit->mapTo(&d, QPoint(0, 0)).y();
        QVERIFY2(y > lastY, qPrintable(field));
        lastY = y;
        // Each has a label naming it.
        bool labelled = false;
        for (const QLabel *label : d.findChildren<QLabel *>())
            labelled = labelled || label->text().remove(QLatin1Char('&')).startsWith(field);
        QVERIFY2(labelled, qPrintable(field));
    }
    auto *summary = d.findChild<QLabel *>(s(u"summaryLabel"));
    auto *problem = d.findChild<QLabel *>(s(u"problemLabel"));
    auto *save = d.findChild<QPushButton *>(s(u"saveButton"));
    auto *cancel = d.findChild<QPushButton *>(s(u"cancelButton"));
    QVERIFY(summary && problem && save && cancel);
    QCOMPARE(summary->text(), s(u"11 - Hunter.flac"));
    QVERIFY(problem->text().isEmpty());
    QVERIFY(save->isDefault());
    QVERIFY(save->isEnabled());
    QVERIFY(d.findChild<QDialogButtonBox *>(s(u"buttons")));
    QVERIFY(save->mapTo(&d, QPoint(0, 0)).y() > lastY); // buttons at the bottom
}

void TagEditorTest::oneTrack()
{
    TagEditorDialog d({ target(11, u"Hunter", u"1997") });
    Recorder rec;
    d.setSaver(rec.saver());
    QSignalSpy saved(&d, &TagEditorDialog::saved);
    QCOMPARE(editor(d, u"Title")->text(), s(u"Hunter"));
    QCOMPARE(editor(d, u"Genre")->text(), s(u"Electronic; Trip Hop"));
    QCOMPARE(editor(d, u"Composer")->text(), QString());
    QVERIFY(editor(d, u"Composer")->placeholderText().isEmpty());
    QVERIFY(d.edits().isEmpty());
    QCOMPARE(d.pendingChanges().size(), 1);
    QVERIFY(d.pendingChanges().at(0).isEmpty());
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));
    type(editor(d, u"Title"), s(u"Hunter (Live)"));
    type(editor(d, u"Track#"), s(u"1"));
    editor(d, u"Year")->clear();
    QCOMPARE(d.edits(), (QMap<QString, QString> { { s(u"Title"), s(u"Hunter (Live)") }, { s(u"Track#"), s(u"1") }, { s(u"Year"), s(u"") } }));
    // Typing a value back to what it was is not an edit.
    type(editor(d, u"Album"), s(u"Post"));
    type(editor(d, u"Album"), s(u"Homogenic"));
    QVERIFY(!d.edits().contains(s(u"Album")));
    QTest::mouseClick(need<QPushButton>(d, s(u"saveButton")), Qt::LeftButton);
    QCOMPARE(rec.paths, QStringList({ s(u"/music/11 - Hunter.flac") }));
    QCOMPARE(rec.changes.at(0).fields,
        (QMap<QString, QStringList> { { s(u"Title"), { s(u"Hunter (Live)") } }, { s(u"Track#"), { s(u"1") } }, { s(u"Year"), { } } }));
    QCOMPARE(saved.size(), 1);
    QCOMPARE(saved.at(0).at(0).value<QList<qint64>>(), QList<qint64>({ 11 }));
    QVERIFY(!d.isVisible());
    QCOMPARE(d.result(), int(QDialog::Accepted));
}

void TagEditorTest::manyTracks()
{
    TagEditorDialog d(three());
    Recorder rec;
    rec.unchangedFor = { s(u"/music/13 - Unravel.flac") };
    d.setSaver(rec.saver());
    QSignalSpy saved(&d, &TagEditorDialog::saved);
    QCOMPARE(need<QLabel>(d, s(u"summaryLabel"))->text(), s(u"3 files"));
    // What all share is shown; what differs is left empty with a hint.
    QCOMPARE(editor(d, u"Album")->text(), s(u"Homogenic"));
    QCOMPARE(editor(d, u"Title")->text(), QString());
    QCOMPARE(editor(d, u"Title")->placeholderText(), s(u"<multiple values>"));
    QCOMPARE(editor(d, u"Year")->placeholderText(), s(u"<multiple values>"));
    QVERIFY(editor(d, u"Album")->placeholderText().isEmpty());
    QVERIFY(editor(d, u"Composer")->placeholderText().isEmpty());
    QVERIFY(d.edits().isEmpty());
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));
    type(editor(d, u"Year"), s(u"1997"));
    type(editor(d, u"Genre"), s(u"Electronic; Trip Hop; Art Pop"));
    QCOMPARE(d.pendingChanges().size(), 3);
    QVERIFY(d.pendingChanges().at(0).fields.contains(s(u"Genre")));
    QVERIFY(!d.pendingChanges().at(0).fields.contains(s(u"Year")));
    QVERIFY(d.pendingChanges().at(1).fields.contains(s(u"Year")));
    QVERIFY(d.save());
    // Every file with something to change is saved, in order; titles are untouched.
    QCOMPARE(rec.paths, QStringList({ s(u"/music/11 - Hunter.flac"), s(u"/music/12 - J\u00f3ga.flac"), s(u"/music/13 - Unravel.flac") }));
    for (const TagChanges &c : std::as_const(rec.changes))
        QVERIFY(!c.fields.contains(s(u"Title")));
    QCOMPARE(rec.changes.at(1).fields.value(s(u"Year")), QStringList({ s(u"1997") }));
    QCOMPARE(rec.changes.at(2).fields.value(s(u"Genre")), QStringList({ s(u"Electronic"), s(u"Trip Hop"), s(u"Art Pop") }));
    // saved() names the files written; "unchanged" is a success but not a write.
    QCOMPARE(saved.size(), 1);
    QCOMPARE(saved.at(0).at(0).value<QList<qint64>>(), QList<qint64>({ 11, 12 }));
    QCOMPARE(d.result(), int(QDialog::Accepted));
}

void TagEditorTest::mixedFieldCanBeCleared()
{
    // E-DIALOG: a field with a hint counts as edited once the user has typed in it,
    // even if what is left is nothing.
    TagEditorDialog d(three());
    Recorder rec;
    d.setSaver(rec.saver());
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));
    QLineEdit *year = editor(d, u"Year");
    year->setFocus();
    QTest::keyClicks(year, s(u"x"));
    QTest::keyClick(year, Qt::Key_Backspace);
    QCOMPARE(year->text(), QString());
    QCOMPARE(d.edits(), (QMap<QString, QString> { { s(u"Year"), s(u"") } }));
    QVERIFY(d.save());
    QCOMPARE(rec.paths.size(), 2); // the two that had a year
    QCOMPARE(rec.changes.at(0).fields, (QMap<QString, QStringList> { { s(u"Year"), { } } }));
    // The same typing in a field every track shares, back to what it was, is no edit.
    TagEditorDialog again(three());
    again.show();
    QVERIFY(QTest::qWaitForWindowExposed(&again));
    QLineEdit *album = editor(again, u"Album");
    album->setFocus();
    album->end(false);
    QTest::keyClicks(album, s(u"x"));
    QTest::keyClick(album, Qt::Key_Backspace);
    QVERIFY(again.edits().isEmpty());
}

void TagEditorTest::refusedEdit()
{
    TagEditorDialog d(three());
    Recorder rec;
    d.setSaver(rec.saver());
    QSignalSpy saved(&d, &TagEditorDialog::saved);
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));
    type(editor(d, u"Album"), s(u"Post"));
    type(editor(d, u"Track#"), s(u"3/12"));
    QVERIFY(!d.save());
    QVERIFY(d.isVisible()); // stays open
    QCOMPARE(need<QLabel>(d, s(u"problemLabel"))->text(), s(u"Track# must be a whole number from 1 to 9999."));
    QTRY_VERIFY(editor(d, u"Track#")->hasFocus()); // on the field at fault
    QVERIFY(rec.paths.isEmpty()); // nothing at all was saved
    QCOMPARE(saved.size(), 0);
    type(editor(d, u"Track#"), s(u"3"));
    QVERIFY(d.save());
    QVERIFY(need<QLabel>(d, s(u"problemLabel"))->text().isEmpty());
    QCOMPARE(rec.paths.size(), 3);
}

void TagEditorTest::failedSave()
{
    TagEditorDialog d(three());
    Recorder rec;
    rec.failFor = { s(u"/music/12 - J\u00f3ga.flac") };
    d.setSaver(rec.saver());
    QSignalSpy saved(&d, &TagEditorDialog::saved);
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));
    type(editor(d, u"Album"), s(u"Post"));
    QVERIFY(!d.save());
    QVERIFY(d.isVisible());
    QCOMPARE(rec.paths.size(), 3); // one failure does not stop the others
    QCOMPARE(saved.size(), 1);
    QCOMPARE(saved.at(0).at(0).value<QList<qint64>>(), QList<qint64>({ 11, 13 }));
    const QString problem = need<QLabel>(d, s(u"problemLabel"))->text();
    QVERIFY2(problem.contains(s(u"12 - J\u00f3ga.flac")), qPrintable(problem));
    QVERIFY2(problem.contains(s(u"1 of 3")), qPrintable(problem));
    // Trying again saves only what is still to do.
    rec.failFor.clear();
    QVERIFY(d.save());
    QCOMPARE(rec.paths.size(), 4);
    QCOMPARE(rec.paths.last(), s(u"/music/12 - J\u00f3ga.flac"));
    QCOMPARE(saved.size(), 2);
    QCOMPARE(saved.at(1).at(0).value<QList<qint64>>(), QList<qint64>({ 12 }));
}

void TagEditorTest::cancel()
{
    TagEditorDialog d(three());
    Recorder rec;
    d.setSaver(rec.saver());
    QSignalSpy saved(&d, &TagEditorDialog::saved);
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));
    type(editor(d, u"Album"), s(u"Post"));
    QTest::mouseClick(need<QPushButton>(d, s(u"cancelButton")), Qt::LeftButton);
    QVERIFY(!d.isVisible());
    QCOMPARE(d.result(), int(QDialog::Rejected));
    QVERIFY(rec.paths.isEmpty());
    QCOMPARE(saved.size(), 0);
    // Save with nothing edited closes without saving anything.
    TagEditorDialog nothing(three());
    nothing.setSaver(rec.saver());
    QVERIFY(nothing.save());
    QVERIFY(rec.paths.isEmpty());
    QCOMPARE(nothing.result(), int(QDialog::Accepted));
}

void TagEditorTest::noTargets()
{
    TagEditorDialog d({ });
    QCOMPARE(need<QLabel>(d, s(u"summaryLabel"))->text(), s(u"0 files"));
    QVERIFY(!need<QPushButton>(d, s(u"saveButton"))->isEnabled());
    QVERIFY(!editor(d, u"Title")->isEnabled());
    QVERIFY(d.pendingChanges().isEmpty());
}

void TagEditorTest::realFile()
{
    // With the saver left alone the dialog writes through writeTags.
    QTemporaryDir dir;
    const QString path = dir.path() + s(u"/basic.flac");
    QVERIFY(QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/basic.flac"), path));
    QVERIFY(QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner));
    const ReadResult before = readTags(path);
    QVERIFY(before.status == ReadResult::Status::Ok);
    EditTarget t;
    t.id = 5;
    t.path = path;
    t.tags = before.tags;
    TagEditorDialog d({ t });
    QSignalSpy saved(&d, &TagEditorDialog::saved);
    QCOMPARE(editor(d, u"Title")->text(), before.tags.first(s(u"Title")));
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));
    type(editor(d, u"Title"), s(u"Edited by the dialog"));
    type(editor(d, u"Genre"), s(u"One; Two"));
    type(editor(d, u"Rating"), s(u"3.5"));
    QVERIFY(d.save());
    QCOMPARE(saved.size(), 1);
    const ReadResult after = readTags(path);
    QCOMPARE(after.tags.first(s(u"Title")), s(u"Edited by the dialog"));
    QCOMPARE(after.tags.fields.value(s(u"Genre")), QStringList({ s(u"One"), s(u"Two") }));
    QCOMPARE(after.tags.first(s(u"Rating")), s(u"3.5"));
    QCOMPARE(after.tags.first(s(u"Artist")), before.tags.first(s(u"Artist"))); // untouched
    QCOMPARE(after.tags.pictures.size(), before.tags.pictures.size());
}

// Added by the reviewer of plan 12 (review round 1).
// Spec: E-DIALOG, Save 2 ("Each track that has something to change is handed
// to the saver") and 4 ("Saving again writes only what is still to do").
// A field edited after a partly failed Save is still to do on every track,
// including the ones whose files were written by the first Save.

void TagEditorTest::editAfterFailedSave()
{
    TagEditorDialog d(three());
    Recorder rec;
    rec.failFor = { s(u"/music/12 - J\u00f3ga.flac") };
    d.setSaver(rec.saver());
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));
    type(editor(d, u"Album"), s(u"Post"));
    QVERIFY(!d.save());
    QCOMPARE(rec.paths.size(), 3);
    // What was written is not written again.
    const QList<TagChanges> between = d.pendingChanges();
    QCOMPARE(between.size(), 3);
    QVERIFY(between.at(0).isEmpty());
    QCOMPARE(between.at(1).fields.keys(), QStringList({ s(u"Album") }));
    QVERIFY(between.at(2).isEmpty());
    // A new edit after the failure reaches every track.
    rec.failFor.clear();
    type(editor(d, u"Grouping"), s(u"Late"));
    QVERIFY(d.save());
    QCOMPARE(rec.paths.size(), 6);
    QCOMPARE(rec.paths.mid(3),
        QStringList({ s(u"/music/11 - Hunter.flac"), s(u"/music/12 - J\u00f3ga.flac"), s(u"/music/13 - Unravel.flac") }));
    QCOMPARE(rec.changes.at(3).fields.keys(), QStringList({ s(u"Grouping") }));
    QCOMPARE(rec.changes.at(4).fields.keys(), QStringList({ s(u"Album"), s(u"Grouping") }));
    QCOMPARE(rec.changes.at(5).fields.keys(), QStringList({ s(u"Grouping") }));
    QCOMPARE(rec.changes.at(3).fields.value(s(u"Grouping")), QStringList({ s(u"Late") }));
}

QTEST_MAIN(TagEditorTest)
#include "tst_ui_tageditor.moc"
