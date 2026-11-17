// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// Love in the window: a field of the tag editor, and a heart in the track list.
// Expected values: docs/spec/tag-editor.md (E-FIELDS, E-CHECK, E-BUILD), docs/spec/track-list.md
// (T-MODEL). Runs on the offscreen platform. Two cases write real files: copies of fixtures in a
// temporary folder.
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QtTest>

#include "tagedit/tagedit.h"
#include "tageditor/tageditordialog.h"
#include "tracklist/tracklistmodel.h"

using namespace linuxbee;
using linuxbee::ui::EditTarget;
using linuxbee::ui::TagEditorDialog;
using linuxbee::ui::TrackListModel;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

const QString kLove = QStringLiteral("Love");
const QString kSentence = QStringLiteral("Love must be L (loved) or B (banned).");

TrackTags tagsWith(const char16_t *love)
{
    TrackTags tags;
    tags.fields.insert(s(u"Title"), { s(u"Hunter") });
    if (*love)
        tags.fields.insert(kLove, { s(love) });
    return tags;
}

EditTarget target(qint64 id, const char16_t *love)
{
    EditTarget t;
    t.id = id;
    t.path = QStringLiteral("/music/%1.flac").arg(id);
    t.tags = tagsWith(love);
    return t;
}

QLineEdit *loveBox(TagEditorDialog &d)
{
    if (auto *found = d.findChild<QLineEdit *>(QStringLiteral("field.Love")))
        return found;
    QTest::qFail("no line editor named field.Love", __FILE__, __LINE__);
    static QLineEdit standIn;
    return &standIn;
}

void type(QLineEdit *edit, const QString &text)
{
    edit->setFocus();
    edit->selectAll();
    if (text.isEmpty())
        QTest::keyClick(edit, Qt::Key_Delete);
    else
        QTest::keyClicks(edit, text);
}

LibraryTrack row(qint64 id, const char16_t *title, const char16_t *love)
{
    LibraryTrack t;
    t.id = id;
    t.path = QStringLiteral("/music/%1.flac").arg(id);
    t.category = QStringLiteral("Music");
    t.tags.fields.insert(s(u"Title"), { s(title) });
    if (*love)
        t.tags.fields.insert(kLove, { s(love) });
    return t;
}

}

class LoveWindowTest : public QObject {
    Q_OBJECT
private slots:
    void itIsAFieldOfTheEditorAfterRating();
    void whatMayBeTyped_data();
    void whatMayBeTyped();
    void whatIsWritten();
    void theDialogShowsAndTakesIt();
    void theDialogRefusesAWord();
    void manyTracks();
    void aRealFile();
    void theListShowsAHeart();
    void theListStillSortsAndFindsByTheLetter();
};

void LoveWindowTest::itIsAFieldOfTheEditorAfterRating()
{
    const QStringList fields = tagEditorFields();
    const int at = int(fields.indexOf(kLove));
    QVERIFY(at > 0);
    QCOMPARE(fields.at(at - 1), s(u"Rating"));
    QCOMPARE(fields.at(at + 1), s(u"Comment"));
    QCOMPARE(fields.count(kLove), 1);
    QVERIFY(!isListField(kLove));
}

void LoveWindowTest::whatMayBeTyped_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<bool>("fine");
    QTest::newRow("L") << s(u"L") << true;
    QTest::newRow("B") << s(u"B") << true;
    QTest::newRow("lower case") << s(u"l") << true;
    QTest::newRow("spaces about it") << s(u"  b ") << true;
    QTest::newRow("nothing: it is removed") << s(u"") << true;
    QTest::newRow("only spaces") << s(u"   ") << true;
    QTest::newRow("a word") << s(u"Love") << false;
    QTest::newRow("yes") << s(u"yes") << false;
    QTest::newRow("both") << s(u"L; B") << false;
    QTest::newRow("a number") << s(u"1") << false;
    QTest::newRow("the heart itself") << s(u"\u2665") << false;
}

void LoveWindowTest::whatMayBeTyped()
{
    QFETCH(QString, text);
    QFETCH(bool, fine);
    QCOMPARE(editProblem(kLove, text), fine ? QString() : kSentence);
    QCOMPARE(editProblem(s(u"love"), text), fine ? QString() : kSentence); // the name in any case
}

void LoveWindowTest::whatIsWritten()
{
    const QList<TrackTags> tracks { tagsWith(u""), tagsWith(u"L"), tagsWith(u"B") };
    // Lower case is written in upper case; a track that has it already is not named.
    QList<TagChanges> c = buildEdits(tracks, { { kLove, s(u" l ") } });
    QCOMPARE(c.size(), 3);
    QCOMPARE(c.at(0).fields, (QMap<QString, QStringList> { { kLove, { s(u"L") } } }));
    QVERIFY(c.at(1).isEmpty());
    QCOMPARE(c.at(2).fields, (QMap<QString, QStringList> { { kLove, { s(u"L") } } }));
    // Nothing removes it, where there is one.
    c = buildEdits(tracks, { { kLove, s(u"") } });
    QVERIFY(c.at(0).isEmpty());
    QCOMPARE(c.at(1).fields, (QMap<QString, QStringList> { { kLove, { } } }));
    QCOMPARE(c.at(2).fields, (QMap<QString, QStringList> { { kLove, { } } }));
    // A word is skipped for every track (E-BUILD).
    c = buildEdits(tracks, { { kLove, s(u"yes") }, { s(u"Title"), s(u"Other") } });
    for (const TagChanges &each : c)
        QCOMPARE(each.fields.keys(), QStringList { s(u"Title") });
    // E-STATE works as for any field.
    QVERIFY(fieldState(tracks, kLove).kind == FieldState::Kind::Mixed);
    QVERIFY(fieldState(tracks.mid(0, 1), kLove).kind == FieldState::Kind::Blank);
    QCOMPARE(fieldState(tracks.mid(1, 1), kLove).text, s(u"L"));
}

void LoveWindowTest::theDialogShowsAndTakesIt()
{
    TagEditorDialog d({ target(1, u"L") });
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));
    QLineEdit *box = loveBox(d);
    QCOMPARE(box->text(), s(u"L"));
    // Below Rating, above Comment, with a label that names it and says what to type.
    auto *rating = d.findChild<QLineEdit *>(s(u"field.Rating"));
    auto *comment = d.findChild<QLineEdit *>(s(u"field.Comment"));
    QVERIFY(rating && comment);
    QVERIFY(box->mapTo(&d, QPoint(0, 0)).y() > rating->mapTo(&d, QPoint(0, 0)).y());
    QVERIFY(box->mapTo(&d, QPoint(0, 0)).y() < comment->mapTo(&d, QPoint(0, 0)).y());
    bool labelled = false;
    for (const QLabel *label : d.findChildren<QLabel *>())
        labelled = labelled || label->text().remove(QLatin1Char('&')).startsWith(kLove);
    QVERIFY(labelled);
    QVERIFY2(box->placeholderText().contains(s(u"L")) && box->placeholderText().contains(s(u"B")),
        "an empty box says what may be typed");
    QVERIFY(d.edits().isEmpty());

    type(box, s(u"b"));
    QCOMPARE(d.edits(), (QMap<QString, QString> { { kLove, s(u"b") } }));
    QCOMPARE(
        d.pendingChanges().at(0).fields, (QMap<QString, QStringList> { { kLove, { s(u"B") } } }));
    type(box, s(u"L")); // back to what it was: not an edit
    QVERIFY(d.edits().isEmpty());
    type(box, QString());
    QCOMPARE(d.pendingChanges().at(0).fields, (QMap<QString, QStringList> { { kLove, { } } }));
}

void LoveWindowTest::theDialogRefusesAWord()
{
    TagEditorDialog d({ target(1, u"") });
    int saves = 0;
    d.setSaver([&saves](const QString &, const TagChanges &) {
        ++saves;
        WriteResult r;
        r.status = WriteResult::Status::Ok;
        return r;
    });
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));
    type(loveBox(d), s(u"yes"));
    QVERIFY(!d.save());
    QCOMPARE(saves, 0);
    auto *problem = d.findChild<QLabel *>(s(u"problemLabel"));
    QVERIFY(problem);
    QCOMPARE(problem->text(), kSentence);
    QVERIFY(loveBox(d)->hasFocus());
    QVERIFY(d.isVisible());
    type(loveBox(d), s(u"l"));
    QVERIFY(d.save());
    QCOMPARE(saves, 1);
}

void LoveWindowTest::manyTracks()
{
    TagEditorDialog d({ target(1, u"L"), target(2, u""), target(3, u"B") });
    QLineEdit *box = loveBox(d);
    QCOMPARE(box->text(), QString());
    QCOMPARE(box->placeholderText(), s(u"<multiple values>")); // E-DIALOG, Mixed
    type(box, s(u"L"));
    const QList<TagChanges> c = d.pendingChanges();
    QVERIFY(c.at(0).isEmpty());
    QCOMPARE(c.at(1).fields, (QMap<QString, QStringList> { { kLove, { s(u"L") } } }));
    QCOMPARE(c.at(2).fields, (QMap<QString, QStringList> { { kLove, { s(u"L") } } }));
}

void LoveWindowTest::aRealFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto copy = [&dir](const char *name) {
        const QString to = dir.filePath(QLatin1String(name));
        if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/") + QLatin1String(name), to))
            return QString();
        QFile::setPermissions(to, QFile::ReadOwner | QFile::WriteOwner);
        return to;
    };
    // Loving a file that had none.
    const QString plain = copy("basic.m4a");
    QVERIFY(!plain.isEmpty());
    {
        EditTarget t;
        t.id = 1;
        t.path = plain;
        t.tags = readTags(plain).tags;
        TagEditorDialog d({ t });
        d.show();
        QVERIFY(QTest::qWaitForWindowExposed(&d));
        QCOMPARE(loveBox(d)->text(), QString());
        type(loveBox(d), s(u"l"));
        QVERIFY(d.save());
    }
    QCOMPARE(readTags(plain).tags.fields.value(kLove), QStringList { s(u"L") });
    // And taking it away again from one that had it.
    const QString loved = copy("love.mp3");
    QVERIFY(!loved.isEmpty());
    {
        EditTarget t;
        t.id = 2;
        t.path = loved;
        t.tags = readTags(loved).tags;
        TagEditorDialog d({ t });
        d.show();
        QVERIFY(QTest::qWaitForWindowExposed(&d));
        QCOMPARE(loveBox(d)->text(), s(u"L"));
        type(loveBox(d), QString());
        QVERIFY(d.save());
    }
    const TrackTags after = readTags(loved).tags;
    QVERIFY(!after.fields.contains(kLove));
    QVERIFY(!after.other.contains(s(u"LOVE RATING")));
}

void LoveWindowTest::theListShowsAHeart()
{
    TrackListModel model;
    QVERIFY(model.setColumns({ s(u"Title"), kLove }));
    model.setGivenOrder(true);
    model.setTracks({ row(1, u"Hunter", u"L"), row(2, u"Joga", u""), row(3, u"Unravel", u"B") });
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.headerData(1, Qt::Horizontal).toString(), kLove);
    QCOMPARE(model.index(0, 1).data().toString(), s(u"\u2665")); // a heart
    QCOMPARE(model.index(1, 1).data().toString(), QString());
    QCOMPARE(model.index(2, 1).data().toString(), s(u"\u2298")); // a barred circle
    QCOMPARE(model.index(0, 0).data().toString(), s(u"Hunter")); // other columns as ever
    // The column under another spelling of its name is the same column.
    QVERIFY(model.setColumns({ s(u"love") }));
    QCOMPARE(model.index(0, 0).data().toString(), s(u"\u2665"));
    // A title that happens to be the letter stays a letter.
    QVERIFY(model.setColumns({ s(u"Title") }));
    model.setTracks({ row(4, u"L", u"") });
    QCOMPARE(model.index(0, 0).data().toString(), s(u"L"));
}

void LoveWindowTest::theListStillSortsAndFindsByTheLetter()
{
    TrackListModel model;
    QVERIFY(model.setColumns({ s(u"Title"), kLove }));
    model.setTracks({ row(1, u"Hunter", u"L"), row(2, u"Joga", u""), row(3, u"Unravel", u"B") });
    model.setSortFields({ kLove, s(u"Title") });
    // B, then L, then the track with none (T-COMPARE: text; an empty value where text sorts it).
    QStringList order;
    for (int i = 0; i < model.rowCount(); ++i)
        order.append(model.index(i, 0).data().toString());
    QCOMPARE(order.indexOf(s(u"Unravel")) + 1, order.indexOf(s(u"Hunter")));
}

QTEST_MAIN(LoveWindowTest)
#include "tst_ui_love.moc"
