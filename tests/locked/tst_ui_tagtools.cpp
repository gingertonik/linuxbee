// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/tag-tools-dialog.md (TT-x). Runs on
// the offscreen platform. The last test writes a real file, a copy of a
// plan-03 fixture in a temporary folder.
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtTest>

#include "tags/tagreader.h"
#include "tagtools/capitalisepanel.h"
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

}

class TagToolsTest : public QObject {
    Q_OBJECT
private slots:
    void structure();
    void preview();
    void previewOfOtherAndPictures();
    void problem();
    void apply();
    void failedApply();
    void cancelAndNothingToDo();
    void remembered();
    void replacePanel();
    void replacePanelRemembers();
    void casePanel();
    void casePanelRemembers();
    void realFile();
};

void TagToolsTest::structure()
{
    // TT-STRUCTURE
    auto *panel = new Script;
    TagToolDialog d(panel, three());
    QCOMPARE(d.objectName(), s(u"tagToolDialog"));
    QCOMPARE(d.windowTitle(), s(u"Scripted Tool"));
    QCOMPARE(d.panel(), panel);
    QVERIFY(d.isAncestorOf(panel));
    auto *table = need<QTableWidget>(d, u"previewTable");
    QCOMPARE(table->columnCount(), 4);
    QStringList headers;
    for (int c = 0; c < table->columnCount(); ++c)
        headers.append(table->horizontalHeaderItem(c) ? table->horizontalHeaderItem(c)->text() : QString());
    QCOMPARE(headers, QStringList({ s(u"File"), s(u"Field"), s(u"Now"), s(u"Becomes") }));
    QCOMPARE(table->rowCount(), 0);
    QCOMPARE(need<QLabel>(d, u"summaryLabel")->text(), s(u"Nothing to change"));
    QVERIFY(!need<QPushButton>(d, u"applyButton")->isEnabled());
    QVERIFY(need<QPushButton>(d, u"cancelButton")->isEnabled());
    QCOMPARE(panel->loads, 0); // no settings were given
    QCOMPARE(panel->seen.size(), 3); // the panel was asked about the tracks
    QCOMPARE(d.pendingChanges().size(), 3);
}

void TagToolsTest::preview()
{
    // TT-PREVIEW
    auto *panel = new Script;
    TagToolDialog d(panel, three());
    auto *table = need<QTableWidget>(d, u"previewTable");
    auto *summary = need<QLabel>(d, u"summaryLabel");
    auto *apply = need<QPushButton>(d, u"applyButton");
    panel->maker = [](const QList<EditTarget> &targets) {
        QList<TagChanges> out(targets.size());
        out[0].fields.insert(s(u"Title"), { s(u"Hello") });
        out[0].fields.insert(s(u"Genre"), { s(u"Rock"), s(u"Pop") }); // the track has none
        out[0].fields.insert(s(u"Album"), { }); // removed
        out[2].fields.insert(s(u"Artist"), { s(u"Them") });
        return out;
    };
    QCOMPARE(table->rowCount(), 0); // not until the panel says its options changed
    panel->changed();
    QCOMPARE(table->rowCount(), 4);
    QCOMPARE(row(table, 0), QStringList({ s(u"01.flac"), s(u"Album"), s(u"the album"), QString() }));
    QCOMPARE(row(table, 1), QStringList({ s(u"01.flac"), s(u"Genre"), QString(), s(u"Rock; Pop") }));
    QCOMPARE(row(table, 2), QStringList({ s(u"01.flac"), s(u"Title"), s(u"hello world"), s(u"Hello") }));
    QCOMPARE(row(table, 3), QStringList({ s(u"03.flac"), s(u"Artist"), s(u"the band"), s(u"Them") }));
    QVERIFY(!(table->item(0, 3)->flags() & Qt::ItemIsEditable));
    QCOMPARE(summary->text(), s(u"2 of 3 tracks would change"));
    QVERIFY(apply->isEnabled());
    const QList<TagChanges> pending = d.pendingChanges();
    QCOMPARE(pending.size(), 3);
    QCOMPARE(pending.at(0).fields.size(), 3);
    QVERIFY(pending.at(1).isEmpty());
    QCOMPARE(pending.at(2).fields.value(s(u"Artist")), QStringList({ s(u"Them") }));

    panel->maker = nullptr;
    panel->changed();
    QCOMPARE(table->rowCount(), 0);
    QCOMPARE(summary->text(), s(u"Nothing to change"));
    QVERIFY(!apply->isEnabled());

    panel->maker = shout;
    panel->changed();
    QCOMPARE(table->rowCount(), 3);
    QCOMPARE(summary->text(), s(u"3 of 3 tracks would change"));
}

void TagToolsTest::previewOfOtherAndPictures()
{
    // TT-PREVIEW: other keys after the fields, the pictures last.
    EditTarget t = target(11, u"01.flac", u"hello", u"band");
    t.tags.other.insert(s(u"MOOD"), { s(u"Calm"), s(u"Blue") });
    t.tags.pictures = { Picture { }, Picture { } };
    auto *panel = new Script;
    panel->maker = [](const QList<EditTarget> &targets) {
        QList<TagChanges> out(targets.size());
        out[0].pictures = QList<Picture> { };
        out[0].other.insert(s(u"MOOD"), { });
        out[0].other.insert(s(u"ACOUSTID"), { s(u"abc") });
        out[0].fields.insert(s(u"Title"), { s(u"Hello") });
        return out;
    };
    TagToolDialog d(panel, { t });
    auto *table = need<QTableWidget>(d, u"previewTable");
    QCOMPARE(table->rowCount(), 4);
    QCOMPARE(row(table, 0), QStringList({ s(u"01.flac"), s(u"Title"), s(u"hello"), s(u"Hello") }));
    QCOMPARE(row(table, 1), QStringList({ s(u"01.flac"), s(u"other ACOUSTID"), QString(), s(u"abc") }));
    QCOMPARE(row(table, 2), QStringList({ s(u"01.flac"), s(u"other MOOD"), s(u"Calm; Blue"), QString() }));
    QCOMPARE(row(table, 3), QStringList({ s(u"01.flac"), s(u"Pictures"), s(u"2"), s(u"0") }));
    QCOMPARE(need<QLabel>(d, u"summaryLabel")->text(), s(u"1 of 1 tracks would change"));
}

void TagToolsTest::problem()
{
    // TT-PREVIEW: while the panel has a problem.
    auto *panel = new Script;
    panel->maker = shout;
    TagToolDialog d(panel, three());
    Recorder rec;
    d.setSaver(rec.saver());
    auto *table = need<QTableWidget>(d, u"previewTable");
    QCOMPARE(table->rowCount(), 3);
    panel->trouble = s(u"That will not do.");
    panel->changed();
    QCOMPARE(table->rowCount(), 0);
    QCOMPARE(need<QLabel>(d, u"summaryLabel")->text(), s(u"That will not do."));
    QVERIFY(!need<QPushButton>(d, u"applyButton")->isEnabled());
    QCOMPARE(d.pendingChanges().size(), 3);
    for (const TagChanges &c : d.pendingChanges())
        QVERIFY(c.isEmpty());
    QVERIFY(!d.apply());
    QCOMPARE(rec.paths, QStringList());
    panel->trouble.clear();
    panel->changed();
    QCOMPARE(table->rowCount(), 3);
    QVERIFY(need<QPushButton>(d, u"applyButton")->isEnabled());
}

void TagToolsTest::apply()
{
    // TT-APPLY
    auto *panel = new Script;
    panel->maker = [](const QList<EditTarget> &targets) {
        QList<TagChanges> out = shout(targets);
        out[1] = TagChanges { }; // the second track is fine as it is
        return out;
    };
    TagToolDialog d(panel, three());
    Recorder rec;
    rec.unchangedFor = { s(u"/music/album/03.flac") };
    d.setSaver(rec.saver());
    QSignalSpy saved(&d, &TagToolDialog::saved);
    QSignalSpy accepted(&d, &QDialog::accepted);
    d.show();
    need<QPushButton>(d, u"applyButton")->click();
    QCOMPARE(rec.paths, QStringList({ s(u"/music/album/01.flac"), s(u"/music/album/03.flac") }));
    QCOMPARE(rec.changes.at(0).fields.value(s(u"Title")), QStringList({ s(u"HELLO WORLD") }));
    QCOMPARE(saved.size(), 1);
    QCOMPARE(saved.at(0).at(0).value<QList<qint64>>(), QList<qint64>({ 11 })); // 13 turned out unchanged
    QCOMPARE(accepted.size(), 1);
    QVERIFY(!d.isVisible());
    QCOMPARE(panel->saves, 0); // no settings were given
}

void TagToolsTest::failedApply()
{
    // TT-APPLY: a failure does not stop the others, and what is left can be tried again.
    auto *panel = new Script;
    panel->maker = shout;
    QTemporaryDir dir;
    QSettings settings(dir.path() + s(u"/s.ini"), QSettings::IniFormat);
    TagToolDialog d(panel, three(), &settings);
    Recorder rec;
    rec.failFor = { s(u"/music/album/02.flac") };
    d.setSaver(rec.saver());
    QSignalSpy saved(&d, &TagToolDialog::saved);
    d.show();
    QVERIFY(!d.apply());
    QCOMPARE(rec.paths.size(), 3);
    QCOMPARE(saved.size(), 1);
    QCOMPARE(saved.at(0).at(0).value<QList<qint64>>(), QList<qint64>({ 11, 13 }));
    QVERIFY(d.isVisible());
    QCOMPARE(need<QLabel>(d, u"summaryLabel")->text(), s(u"Could not save 1 of 3: 02.flac"));
    QCOMPARE(panel->saves, 0); // not a complete success: nothing remembered
    auto *table = need<QTableWidget>(d, u"previewTable");
    QCOMPARE(table->rowCount(), 1); // only what is still to do
    QCOMPARE(row(table, 0), QStringList({ s(u"02.flac"), s(u"Title"), s(u"Second Song"), s(u"SECOND SONG") }));
    QCOMPARE(panel->seen.at(0).tags.first(s(u"Title")), s(u"HELLO WORLD")); // the saved tracks have their new tags
    QVERIFY(need<QPushButton>(d, u"applyButton")->isEnabled());

    rec.failFor.clear();
    QVERIFY(d.apply());
    QCOMPARE(rec.paths.size(), 4); // only the one that was left
    QCOMPARE(rec.paths.last(), s(u"/music/album/02.flac"));
    QCOMPARE(saved.size(), 2);
    QCOMPARE(saved.at(1).at(0).value<QList<qint64>>(), QList<qint64>({ 12 }));
    QVERIFY(!d.isVisible());
    QCOMPARE(panel->saves, 1);
}

void TagToolsTest::cancelAndNothingToDo()
{
    auto *panel = new Script;
    QTemporaryDir dir;
    QSettings settings(dir.path() + s(u"/s.ini"), QSettings::IniFormat);
    TagToolDialog d(panel, three(), &settings);
    Recorder rec;
    d.setSaver(rec.saver());
    QSignalSpy saved(&d, &TagToolDialog::saved);
    d.show();
    QVERIFY(!d.apply()); // nothing to change: nothing happens
    QVERIFY(d.isVisible());
    panel->maker = shout;
    panel->changed();
    QSignalSpy rejected(&d, &QDialog::rejected);
    need<QPushButton>(d, u"cancelButton")->click();
    QCOMPARE(rejected.size(), 1);
    QCOMPARE(rec.paths, QStringList());
    QCOMPARE(saved.size(), 0);
    QCOMPARE(panel->saves, 0);

    // No tracks at all.
    auto *lonely = new Script;
    lonely->maker = shout;
    TagToolDialog none(lonely, { });
    QCOMPARE(need<QLabel>(none, u"summaryLabel")->text(), s(u"Nothing to change"));
    QVERIFY(!need<QPushButton>(none, u"applyButton")->isEnabled());
    QVERIFY(!none.apply());
}

void TagToolsTest::remembered()
{
    // TT-REMEMBER
    QTemporaryDir dir;
    QSettings settings(dir.path() + s(u"/s.ini"), QSettings::IniFormat);
    settings.setValue(s(u"script/value"), s(u"from before"));
    auto *panel = new Script;
    panel->maker = [panel](const QList<EditTarget> &targets) {
        // the preview is worked out after the options were loaded
        return panel->loads == 1 ? shout(targets) : QList<TagChanges>(targets.size());
    };
    TagToolDialog d(panel, three(), &settings);
    QCOMPARE(panel->loads, 1);
    QCOMPARE(panel->loaded, s(u"from before"));
    QCOMPARE(need<QTableWidget>(d, u"previewTable")->rowCount(), 3);
    Recorder rec;
    d.setSaver(rec.saver());
    QVERIFY(d.apply());
    QCOMPARE(panel->saves, 1);
    QCOMPARE(settings.value(s(u"script/value")).toString(), s(u"stored"));
}

void TagToolsTest::replacePanel()
{
    // TT-REPLACE
    auto *panel = new SearchReplacePanel;
    TagToolDialog d(panel, three());
    QCOMPARE(d.windowTitle(), s(u"Search and Replace"));
    auto *find = need<QLineEdit>(*panel, u"findEdit");
    auto *replace = need<QLineEdit>(*panel, u"replaceEdit");
    auto *field = need<QComboBox>(*panel, u"fieldCombo");
    auto *regex = need<QCheckBox>(*panel, u"regexBox");
    auto *matchCase = need<QCheckBox>(*panel, u"matchCaseBox");
    auto *wholeWord = need<QCheckBox>(*panel, u"wholeWordBox");
    auto *table = need<QTableWidget>(d, u"previewTable");
    auto *summary = need<QLabel>(d, u"summaryLabel");

    QStringList offered;
    for (int i = 0; i < field->count(); ++i)
        offered.append(field->itemText(i));
    QCOMPARE(offered, QStringList({ s(u"All fields") }) + offeredFields());
    QCOMPARE(field->currentIndex(), 0);
    QVERIFY(!regex->isChecked() && !matchCase->isChecked() && !wholeWord->isChecked());

    // Nothing typed yet: the core's own complaint, and nothing to apply.
    SearchReplaceOptions o = panel->options();
    QCOMPARE(o.find, QString());
    QCOMPARE(o.fields, QStringList());
    QVERIFY(!panel->problem().isEmpty());
    QCOMPARE(panel->problem(), validateSearchReplace(o));
    QCOMPARE(summary->text(), panel->problem());
    QVERIFY(!need<QPushButton>(d, u"applyButton")->isEnabled());

    QSignalSpy changed(panel, &ToolPanel::optionsChanged);
    QTest::keyClicks(find, s(u"the"));
    QVERIFY(changed.size() >= 1);
    QTest::keyClicks(replace, s(u"A"));
    QCOMPARE(panel->problem(), QString());
    o = panel->options();
    QCOMPARE(o.find, s(u"the"));
    QCOMPARE(o.replace, s(u"A"));
    // Case ignored, all fields: Artist of all three and Album of all three.
    QCOMPARE(table->rowCount(), 6);
    QCOMPARE(row(table, 0), QStringList({ s(u"01.flac"), s(u"Album"), s(u"the album"), s(u"A album") }));
    QCOMPARE(row(table, 1), QStringList({ s(u"01.flac"), s(u"Artist"), s(u"the band"), s(u"A band") }));
    QCOMPARE(summary->text(), s(u"3 of 3 tracks would change"));

    field->setCurrentIndex(field->findText(s(u"Artist")));
    QCOMPARE(panel->options().fields, QStringList({ s(u"Artist") }));
    QCOMPARE(table->rowCount(), 3);
    matchCase->setChecked(true);
    QVERIFY(panel->options().matchCase);
    QCOMPARE(table->rowCount(), 2); // "The Band" no longer matches
    QCOMPARE(summary->text(), s(u"2 of 3 tracks would change"));
    matchCase->setChecked(false);

    field->setCurrentIndex(field->findText(s(u"Title")));
    find->setText(s(u"world"));
    replace->setText(s(u"moon"));
    QCOMPARE(table->rowCount(), 2);
    wholeWord->setChecked(true);
    QVERIFY(panel->options().wholeWord);
    QCOMPARE(table->rowCount(), 2); // an apostrophe ends a word
    QCOMPARE(row(table, 1), QStringList({ s(u"03.flac"), s(u"Title"), s(u"world's end"), s(u"moon's end") }));
    wholeWord->setChecked(false);

    regex->setChecked(true);
    find->setText(s(u"^(\\w+) (\\w+)$"));
    replace->setText(s(u"$2 $1"));
    QVERIFY(panel->options().regex);
    QCOMPARE(row(table, 0), QStringList({ s(u"01.flac"), s(u"Title"), s(u"hello world"), s(u"world hello") }));
    find->setText(s(u"(unclosed"));
    QVERIFY(!panel->problem().isEmpty());
    QCOMPARE(table->rowCount(), 0);
    QCOMPARE(summary->text(), panel->problem());

    // The panel's answers are the core's.
    find->setText(s(u"o"));
    regex->setChecked(false);
    const QList<EditTarget> tracks = three();
    const QList<TagChanges> got = panel->changes(tracks);
    QCOMPARE(got.size(), 3);
    for (int i = 0; i < 3; ++i)
        QCOMPARE(got.at(i).fields, searchReplace(tracks.at(i).tags, panel->options()).fields);
}

void TagToolsTest::replacePanelRemembers()
{
    QTemporaryDir dir;
    QSettings settings(dir.path() + s(u"/s.ini"), QSettings::IniFormat);
    {
        auto *panel = new SearchReplacePanel;
        TagToolDialog d(panel, three(), &settings);
        Recorder rec;
        d.setSaver(rec.saver());
        need<QLineEdit>(*panel, u"findEdit")->setText(s(u"band"));
        need<QLineEdit>(*panel, u"replaceEdit")->setText(s(u"group"));
        auto *field = need<QComboBox>(*panel, u"fieldCombo");
        field->setCurrentIndex(field->findText(s(u"Artist")));
        need<QCheckBox>(*panel, u"matchCaseBox")->setChecked(true);
        need<QCheckBox>(*panel, u"wholeWordBox")->setChecked(true);
        QVERIFY(d.apply());
        QCOMPARE(rec.paths.size(), 2);
    }
    QSettings later(nextRun(settings), QSettings::IniFormat); // as the next run reads it
    QCOMPARE(later.value(s(u"tagtools/replace/field")).toString(), s(u"Artist"));
    {
        auto *panel = new SearchReplacePanel;
        TagToolDialog d(panel, three(), &later);
        const SearchReplaceOptions o = panel->options();
        QCOMPARE(o.fields, QStringList({ s(u"Artist") }));
        QVERIFY(o.matchCase);
        QVERIFY(o.wholeWord);
        QVERIFY(!o.regex);
        QCOMPARE(o.find, QString()); // the texts are not remembered
        QCOMPARE(o.replace, QString());
    }
    // Cancel stores nothing; a stored field that is not offered means all fields.
    settings.setValue(s(u"tagtools/replace/field"), s(u"Rating"));
    settings.setValue(s(u"tagtools/replace/regex"), s(u"true"));
    QSettings odd(nextRun(settings), QSettings::IniFormat);
    {
        auto *panel = new SearchReplacePanel;
        TagToolDialog d(panel, three(), &odd);
        QCOMPARE(panel->options().fields, QStringList());
        QVERIFY(panel->options().regex);
        need<QCheckBox>(*panel, u"regexBox")->setChecked(false);
        need<QLineEdit>(*panel, u"findEdit")->setText(s(u"band"));
        d.reject();
    }
    odd.sync();
    QCOMPARE(odd.value(s(u"tagtools/replace/regex")).toString(), s(u"true"));
}

void TagToolsTest::casePanel()
{
    // TT-CASE
    auto *panel = new CapitalisePanel;
    TagToolDialog d(panel, three());
    QCOMPARE(d.windowTitle(), s(u"Capitalise"));
    auto *style = need<QComboBox>(*panel, u"styleCombo");
    auto *fields = need<QListWidget>(*panel, u"fieldList");
    auto *table = need<QTableWidget>(d, u"previewTable");
    QStringList texts, ids, rows;
    for (int i = 0; i < style->count(); ++i) {
        texts.append(style->itemText(i));
        ids.append(style->itemData(i).toString());
    }
    QCOMPARE(texts, QStringList({ s(u"Each Word"), s(u"Title Case"), s(u"Sentence case"), s(u"UPPER CASE"), s(u"lower case") }));
    QCOMPARE(ids, QStringList({ s(u"each-word"), s(u"title"), s(u"sentence"), s(u"upper"), s(u"lower") }));
    for (int i = 0; i < fields->count(); ++i)
        rows.append(fields->item(i)->text());
    QCOMPARE(rows, offeredFields());
    QCOMPARE(ticked(fields), QStringList({ s(u"Title"), s(u"Artist"), s(u"Album"), s(u"Album Artist") }));
    CaseOptions o = panel->options();
    QVERIFY(o.style == CaseStyle::EachWord);
    QCOMPARE(o.fields, ticked(fields));
    QCOMPARE(o.smallWords, QStringList());
    QCOMPARE(panel->problem(), QString());

    // Each Word over Title, Artist and Album: three changes in the first and third track, the
    // album alone in the second, whose title and artist are right already.
    QCOMPARE(table->rowCount(), 7);
    QCOMPARE(row(table, 0), QStringList({ s(u"01.flac"), s(u"Album"), s(u"the album"), s(u"The Album") }));
    QCOMPARE(row(table, 2), QStringList({ s(u"01.flac"), s(u"Title"), s(u"hello world"), s(u"Hello World") }));
    QCOMPARE(row(table, 3), QStringList({ s(u"02.flac"), s(u"Album"), s(u"the album"), s(u"The Album") }));

    const CaseStyle styles[] { CaseStyle::EachWord, CaseStyle::TitleCase, CaseStyle::Sentence, CaseStyle::Upper, CaseStyle::Lower };
    QSignalSpy changed(panel, &ToolPanel::optionsChanged);
    for (int i = 4; i >= 0; --i) {
        style->setCurrentIndex(i);
        QVERIFY(panel->options().style == styles[i]);
    }
    QVERIFY(changed.size() >= 4);
    style->setCurrentIndex(3); // UPPER CASE
    tick(fields, s(u"Artist"), false);
    tick(fields, s(u"Album"), false);
    tick(fields, s(u"Album Artist"), false);
    QCOMPARE(panel->options().fields, QStringList({ s(u"Title") }));
    QCOMPARE(table->rowCount(), 3);
    QCOMPARE(row(table, 2), QStringList({ s(u"03.flac"), s(u"Title"), s(u"world's end"), s(u"WORLD'S END") }));
    tick(fields, s(u"Composer"), true); // a field the tracks do not have changes nothing
    QCOMPARE(panel->options().fields, QStringList({ s(u"Title"), s(u"Composer") }));
    QCOMPARE(table->rowCount(), 3);
    tick(fields, s(u"Composer"), false);

    tick(fields, s(u"Title"), false);
    QCOMPARE(panel->problem(), s(u"Choose at least one field"));
    QCOMPARE(need<QLabel>(d, u"summaryLabel")->text(), s(u"Choose at least one field"));
    QCOMPARE(table->rowCount(), 0);
    for (const TagChanges &c : panel->changes(three()))
        QVERIFY(c.isEmpty()); // not the core's "empty means the default four"
    tick(fields, s(u"Artist"), true);
    QCOMPARE(panel->problem(), QString());

    // The panel's answers are the core's.
    const QList<EditTarget> tracks = three();
    const QList<TagChanges> got = panel->changes(tracks);
    QCOMPARE(got.size(), 3);
    for (int i = 0; i < 3; ++i)
        QCOMPARE(got.at(i).fields, capitalise(tracks.at(i).tags, panel->options()).fields);
}

void TagToolsTest::casePanelRemembers()
{
    QTemporaryDir dir;
    QSettings settings(dir.path() + s(u"/s.ini"), QSettings::IniFormat);
    {
        auto *panel = new CapitalisePanel;
        TagToolDialog d(panel, three(), &settings);
        Recorder rec;
        d.setSaver(rec.saver());
        auto *style = need<QComboBox>(*panel, u"styleCombo");
        style->setCurrentIndex(style->findData(s(u"title")));
        auto *fields = need<QListWidget>(*panel, u"fieldList");
        tick(fields, s(u"Artist"), false);
        tick(fields, s(u"Album"), false);
        tick(fields, s(u"Album Artist"), false);
        QVERIFY(d.apply());
    }
    QSettings later(nextRun(settings), QSettings::IniFormat); // as the next run reads it; one name is plain text there
    QCOMPARE(later.value(s(u"tagtools/case/style")).toString(), s(u"title"));
    {
        auto *panel = new CapitalisePanel;
        TagToolDialog d(panel, three(), &later);
        QVERIFY(panel->options().style == CaseStyle::TitleCase);
        QCOMPARE(panel->options().fields, QStringList({ s(u"Title") }));
        QCOMPARE(need<QTableWidget>(d, u"previewTable")->rowCount(), 2); // previewed with the loaded options
    }
    settings.setValue(s(u"tagtools/case/style"), s(u"sideways"));
    settings.setValue(s(u"tagtools/case/fields"), QStringList { s(u"Nonsense"), s(u"Rating") });
    QSettings odd(nextRun(settings), QSettings::IniFormat);
    {
        auto *panel = new CapitalisePanel;
        TagToolDialog d(panel, three(), &odd);
        QVERIFY(panel->options().style == CaseStyle::EachWord);
        QCOMPARE(panel->options().fields, QStringList({ s(u"Title"), s(u"Artist"), s(u"Album"), s(u"Album Artist") }));
    }
    settings.setValue(s(u"tagtools/case/fields"), QStringList { s(u"Nonsense"), s(u"Genre"), s(u"Album") });
    QSettings mixed(nextRun(settings), QSettings::IniFormat);
    {
        auto *panel = new CapitalisePanel;
        TagToolDialog d(panel, three(), &mixed);
        QCOMPARE(panel->options().fields, QStringList({ s(u"Album"), s(u"Genre") })); // list order; the unknown name ignored
    }
}

void TagToolsTest::realFile()
{
    // The default saver writes the file.
    QTemporaryDir dir;
    const QString path = dir.path() + s(u"/real.flac");
    QVERIFY(QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA) + s(u"/tags/basic.flac"), path));
    const ReadResult before = readTags(path);
    QVERIFY(before.status == ReadResult::Status::Ok);
    QCOMPARE(before.tags.first(s(u"Title")), s(u"Flac Title"));
    auto *panel = new SearchReplacePanel;
    TagToolDialog d(panel, { EditTarget { 7, path, before.tags } });
    QSignalSpy saved(&d, &TagToolDialog::saved);
    need<QLineEdit>(*panel, u"findEdit")->setText(s(u"Flac"));
    need<QLineEdit>(*panel, u"replaceEdit")->setText(s(u"Lossless"));
    auto *table = need<QTableWidget>(d, u"previewTable");
    QCOMPARE(table->rowCount(), 2); // Title and Album
    QVERIFY(d.apply());
    QCOMPARE(saved.size(), 1);
    QCOMPARE(saved.at(0).at(0).value<QList<qint64>>(), QList<qint64>({ 7 }));
    const ReadResult after = readTags(path);
    QCOMPARE(after.tags.first(s(u"Title")), s(u"Lossless Title"));
    QCOMPARE(after.tags.first(s(u"Album")), s(u"Lossless Album"));
    QCOMPARE(after.tags.fields.value(s(u"Artist")), before.tags.fields.value(s(u"Artist")));
}

QTEST_MAIN(TagToolsTest)
#include "tst_ui_tagtools.moc"
