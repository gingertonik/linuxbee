// LOCKED ACCEPTANCE TEST (the tagging-tool dialog asks before a change that cannot be taken back). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/tag-tools-dialog.md (TT-WARN,
// TT-REMOVE). Runs on the offscreen platform; no file is written, and the
// question is answered by a function of the test, never by a dialog.
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtTest>

#include "tags/tagreader.h"
#include "tagtools/capitalisepanel.h"
#include "tagtools/inferpanel.h"
#include "tagtools/removetagspanel.h"
#include "tagtools/renumberpanel.h"
#include "tagtools/searchreplacepanel.h"
#include "tagtools/tagtooldialog.h"

using namespace linuxbee;
using namespace linuxbee::ui;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

// A settings file as the next run of the program reads it. Within one run Qt
// hands back the value that was put in (a bool stays a bool); read from disk
// by a later run, every value is text. Copying the file gives the later view.
QString nextRun(QSettings &settings)
{
    static int count = 0;
    settings.sync();
    const QString copy = settings.fileName() + QStringLiteral(".run%1.ini").arg(++count);
    QFile::remove(copy);
    QFile::copy(settings.fileName(), copy);
    return copy;
}

EditTarget target(qint64 id, const char16_t *file, const char16_t *title, const char16_t *artist)
{
    EditTarget t;
    t.id = id;
    t.path = s(u"/music/album/") + s(file);
    t.tags.fields.insert(s(u"Title"), { s(title) });
    t.tags.fields.insert(s(u"Artist"), { s(artist) });
    t.tags.fields.insert(s(u"Album"), { s(u"the album") });
    return t;
}

QList<EditTarget> three()
{
    return { target(11, u"01.flac", u"hello world", u"the band"), target(12, u"02.flac", u"Second Song", u"The Band"),
        target(13, u"03.flac", u"world's end", u"the band") };
}

// A panel whose answers the test sets.
class Script : public ToolPanel {
public:
    using ToolPanel::ToolPanel;
    QString title() const override { return QStringLiteral("Scripted Tool"); }
    QString problem() const override { return trouble; }
    QList<TagChanges> changes(const QList<EditTarget> &targets) const override
    {
        ++asked;
        seen = targets;
        return maker ? maker(targets) : QList<TagChanges>(targets.size());
    }
    void load(QSettings &settings) override
    {
        ++loads;
        loaded = settings.value(QStringLiteral("script/value")).toString();
    }
    void save(QSettings &settings) const override
    {
        ++saves;
        settings.setValue(QStringLiteral("script/value"), QStringLiteral("stored"));
    }
    void changed() { emit optionsChanged(); }

    QString trouble;
    std::function<QList<TagChanges>(const QList<EditTarget> &)> maker;
    mutable int asked = 0;
    mutable int saves = 0;
    mutable QList<EditTarget> seen;
    int loads = 0;
    QString loaded;
};

// Sets Title to its upper case wherever that is a change.
QList<TagChanges> shout(const QList<EditTarget> &targets)
{
    QList<TagChanges> out;
    for (const EditTarget &t : targets) {
        TagChanges c;
        const QString now = t.tags.first(QStringLiteral("Title"));
        if (now != now.toUpper())
            c.fields.insert(QStringLiteral("Title"), { now.toUpper() });
        out.append(c);
    }
    return out;
}

struct Recorder {
    QStringList paths;
    QList<TagChanges> changes;
    QStringList failFor;
    QStringList unchangedFor;
    TagToolDialog::Saver saver()
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

template <typename T> T *need(QWidget &in, const char16_t *name)
{
    if (T *found = in.findChild<T *>(s(name)))
        return found;
    QTest::qFail(qPrintable(QStringLiteral("no widget named ") + s(name)), __FILE__, __LINE__);
    static T standIn;
    return &standIn;
}

QStringList row(QTableWidget *table, int r)
{
    QStringList out;
    for (int c = 0; c < table->columnCount(); ++c)
        out.append(table->item(r, c) ? table->item(r, c)->text() : QStringLiteral("<no item>"));
    return out;
}

QStringList ticked(QListWidget *list)
{
    QStringList out;
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->checkState() == Qt::Checked)
            out.append(list->item(i)->text());
    return out;
}

void tick(QListWidget *list, const QString &name, bool on)
{
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->text() == name) {
            list->item(i)->setCheckState(on ? Qt::Checked : Qt::Unchecked);
            return;
        }
    QTest::qFail(qPrintable(QStringLiteral("no row ") + name), __FILE__, __LINE__);
}

QStringList offeredFields()
{
    QStringList names = tagFieldNames();
    names.removeAll(QStringLiteral("Rating"));
    return names;
}

EditTarget at(qint64 id, const QString &path)
{
    EditTarget t;
    t.id = id;
    t.path = path;
    return t;
}

bool same(const TagChanges &a, const TagChanges &b)
{
    return a.fields == b.fields && a.other == b.other && a.pictures.has_value() == b.pictures.has_value()
        && (!a.pictures.has_value() || a.pictures->size() == b.pictures->size());
}

#define NEED_PANEL(panel) \
    do { \
        if ((panel)->title().isEmpty()) \
            QFAIL("this panel has not been built"); \
    } while (false)

// A scripted panel that has something to warn about.
class Warned : public Script {
public:
    using Script::Script;
    QString warning(int tracks) const override
    {
        askedFor.append(tracks);
        return text.isEmpty() ? QString() : text.arg(tracks);
    }
    QString text;
    mutable QList<int> askedFor;
};

// The answers the test gives, and what it was asked.
struct Answer {
    bool yes = true;
    QStringList asked;
    TagToolDialog::Confirm confirm()
    {
        return [this](const QString &question) {
            asked.append(question);
            return yes;
        };
    }
};

const QString kAll = QStringLiteral("Remove every tag from %1? The rating and tags that are not in the list go too. Embedded pictures stay. This cannot be undone.");
const QString kAllAndPictures = QStringLiteral("Remove every tag and the embedded pictures from %1? The rating and tags that are not in the list go too. This cannot be undone.");
const QString kPictures = QStringLiteral("Remove the embedded pictures from %1? This cannot be undone.");

}

class TagToolWarnTest : public QObject {
    Q_OBJECT
private slots:
    void theDialogAsks();
    void notAskedWithoutCause();
    void removeTagsWarns();
    void removeTagsThroughTheDialog();
    void theOtherToolsDoNot();
};

void TagToolWarnTest::theDialogAsks()
{
    // TT-WARN: a panel's warning is asked before anything is written; "no" writes nothing and the dialog stays.
    (void)&nextRun;
    (void)&offeredFields;
    (void)&at;
    (void)&same;
    (void)&ticked;
    (void)&row;
    auto *panel = new Warned;
    panel->maker = shout; // all three titles would change
    panel->text = QStringLiteral("Really shout at %1 tracks?");
    QTemporaryDir dir;
    QSettings settings(dir.path() + s(u"/s.ini"), QSettings::IniFormat);
    TagToolDialog d(panel, three(), &settings);
    Recorder rec;
    d.setSaver(rec.saver());
    Answer answer;
    d.setConfirm(answer.confirm());
    QSignalSpy saved(&d, &TagToolDialog::saved);
    QSignalSpy finished(&d, &QDialog::finished);
    auto *table = need<QTableWidget>(d, u"previewTable");
    QCOMPARE(table->rowCount(), 3);
    QCOMPARE(answer.asked, QStringList()); // a preview asks nothing

    answer.yes = false;
    panel->askedFor.clear();
    QVERIFY(!d.apply());
    QCOMPARE(answer.asked, QStringList({ s(u"Really shout at 3 tracks?") }));
    QCOMPARE(panel->askedFor, QList<int>({ 3 })); // the tracks that would change
    QCOMPARE(rec.paths, QStringList());
    QCOMPARE(saved.count(), 0);
    QCOMPARE(finished.count(), 0); // still open
    QCOMPARE(panel->saves, 0); // the options are not stored
    QCOMPARE(table->rowCount(), 3); // and everything is still to do
    QVERIFY(need<QPushButton>(d, u"applyButton")->isEnabled());

    // The button asks as well, each time.
    need<QPushButton>(d, u"applyButton")->click();
    QCOMPARE(answer.asked.size(), 2);
    QCOMPARE(rec.paths, QStringList());

    answer.yes = true;
    QVERIFY(d.apply());
    QCOMPARE(answer.asked.size(), 3);
    QCOMPARE(rec.paths.size(), 3);
    QCOMPARE(saved.count(), 1);
    QCOMPARE(finished.count(), 1);
    QCOMPARE(panel->saves, 1);
}

void TagToolWarnTest::notAskedWithoutCause()
{
    // TT-WARN: no warning, a problem, or nothing to change: no question.
    Answer answer;
    Recorder rec;
    {
        auto *panel = new Warned; // no text: nothing to warn about
        panel->maker = shout;
        TagToolDialog d(panel, three());
        d.setSaver(rec.saver());
        d.setConfirm(answer.confirm());
        QVERIFY(d.apply());
        QCOMPARE(answer.asked, QStringList());
        QCOMPARE(rec.paths.size(), 3);
    }
    {
        auto *panel = new Warned;
        panel->maker = shout;
        panel->text = QStringLiteral("Really %1?");
        panel->trouble = QStringLiteral("Something is wrong");
        TagToolDialog d(panel, three());
        d.setSaver(rec.saver());
        d.setConfirm(answer.confirm());
        QVERIFY(!d.apply());
        QCOMPARE(answer.asked, QStringList());
    }
    {
        auto *panel = new Warned; // no maker: nothing changes
        panel->text = QStringLiteral("Really %1?");
        TagToolDialog d(panel, three());
        d.setSaver(rec.saver());
        d.setConfirm(answer.confirm());
        QVERIFY(!d.apply());
        QCOMPARE(answer.asked, QStringList());
    }
    QCOMPARE(rec.paths.size(), 3);
    // Only one of three would change: the question says one.
    {
        auto *panel = new Warned;
        panel->text = QStringLiteral("Really %1?");
        panel->maker = [](const QList<EditTarget> &targets) {
            QList<TagChanges> out(targets.size());
            out[1].fields.insert(QStringLiteral("Title"), { QStringLiteral("Other") });
            return out;
        };
        TagToolDialog d(panel, three());
        d.setSaver(rec.saver());
        d.setConfirm(answer.confirm());
        QVERIFY(d.apply());
        QCOMPARE(answer.asked, QStringList({ s(u"Really 1?") }));
    }
}

void TagToolWarnTest::removeTagsWarns()
{
    // TT-REMOVE: "All tags" and "Embedded pictures" cannot be taken back, and say so; single fields ask nothing.
    auto *panel = new RemoveTagsPanel;
    NEED_PANEL(panel);
    TagToolDialog d(panel, three());
    auto *fields = need<QListWidget>(*panel, u"fieldList");
    auto *all = need<QCheckBox>(*panel, u"allTagsBox");
    auto *pictures = need<QCheckBox>(*panel, u"picturesBox");
    QCOMPARE(panel->warning(3), QString());
    tick(fields, s(u"Album"), true);
    tick(fields, s(u"Title"), true);
    QCOMPARE(panel->warning(3), QString());
    all->setChecked(true);
    QCOMPARE(panel->warning(3), kAll.arg(s(u"3 files")));
    QCOMPARE(panel->warning(1), kAll.arg(s(u"1 file")));
    QCOMPARE(panel->warning(17000), kAll.arg(s(u"17,000 files")));
    pictures->setChecked(true);
    QCOMPARE(panel->warning(2), kAllAndPictures.arg(s(u"2 files")));
    all->setChecked(false);
    QCOMPARE(panel->warning(2), kPictures.arg(s(u"2 files"))); // with fields ticked beside it
    tick(fields, s(u"Album"), false);
    tick(fields, s(u"Title"), false);
    QCOMPARE(panel->warning(1), kPictures.arg(s(u"1 file")));
    pictures->setChecked(false);
    QCOMPARE(panel->warning(1), QString());
}

void TagToolWarnTest::removeTagsThroughTheDialog()
{
    // TT-REMOVE with TT-WARN: "no" to the question leaves every tag where it is.
    auto *panel = new RemoveTagsPanel;
    NEED_PANEL(panel);
    TagToolDialog d(panel, three());
    Recorder rec;
    d.setSaver(rec.saver());
    Answer answer;
    answer.yes = false;
    d.setConfirm(answer.confirm());
    need<QCheckBox>(*panel, u"allTagsBox")->setChecked(true);
    QVERIFY(!d.apply());
    QCOMPARE(answer.asked, QStringList({ kAll.arg(s(u"3 files")) }));
    QCOMPARE(rec.paths, QStringList());
    QVERIFY(need<QCheckBox>(*panel, u"allTagsBox")->isChecked()); // the options are as they were
    answer.yes = true;
    QVERIFY(d.apply());
    QCOMPARE(rec.paths.size(), 3);
    QCOMPARE(rec.changes.at(0).fields.value(s(u"Title"), { s(u"kept") }), QStringList());

    // One field alone: written without a question.
    auto *plain = new RemoveTagsPanel;
    TagToolDialog e(plain, three());
    Recorder rec2;
    e.setSaver(rec2.saver());
    Answer never;
    e.setConfirm(never.confirm());
    tick(need<QListWidget>(*plain, u"fieldList"), s(u"Album"), true);
    QVERIFY(e.apply());
    QCOMPARE(never.asked, QStringList());
    QCOMPARE(rec2.paths.size(), 3);
}

void TagToolWarnTest::theOtherToolsDoNot()
{
    // TT-WARN: the tools whose changes are ordinary tag edits warn about nothing.
    SearchReplacePanel replace;
    CapitalisePanel capitalise;
    RenumberPanel renumber;
    InferPanel infer;
    for (const ToolPanel *panel : { static_cast<const ToolPanel *>(&replace), static_cast<const ToolPanel *>(&capitalise),
             static_cast<const ToolPanel *>(&renumber), static_cast<const ToolPanel *>(&infer) })
        QCOMPARE(panel->warning(5000), QString());
}

QTEST_MAIN(TagToolWarnTest)
#include "tst_ui_tagtools_warn.moc"
