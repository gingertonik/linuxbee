// LOCKED ACCEPTANCE TEST (the tagging-tool dialog: Remove Tags, Renumber Tracks, Infer Tags From Filename). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/tag-tools-dialog.md (TT-REMOVE,
// TT-RENUMBER, TT-INFER) and, for what a tool changes, the functions of
// tagtools/tagtools.h (docs/spec/tagging-tools.md). Runs on the offscreen
// platform; no file is written.
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

}

class TagToolPanelsTest : public QObject {
    Q_OBJECT
private slots:
    void removePanel();
    void removePanelRemembersNothing();
    void renumberPanel();
    void renumberPanelRemembers();
    void inferPanel();
    void inferPanelRemembers();
};

void TagToolPanelsTest::removePanel()
{
    // TT-REMOVE
    (void)&shout;
    (void)&nextRun;
    QList<EditTarget> tracks = three();
    tracks[0].tags.other.insert(s(u"MOOD"), { s(u"calm") });
    tracks[0].tags.pictures.append(Picture { });
    tracks[0].tags.pictures.append(Picture { });
    auto *panel = new RemoveTagsPanel;
    NEED_PANEL(panel);
    TagToolDialog d(panel, tracks);
    QCOMPARE(d.windowTitle(), s(u"Remove Tags"));
    auto *fields = need<QListWidget>(*panel, u"fieldList");
    auto *all = need<QCheckBox>(*panel, u"allTagsBox");
    auto *pictures = need<QCheckBox>(*panel, u"picturesBox");
    auto *table = need<QTableWidget>(d, u"previewTable");
    QStringList rows;
    for (int i = 0; i < fields->count(); ++i)
        rows.append(fields->item(i)->text());
    QCOMPARE(rows, offeredFields());
    QCOMPARE(all->text(), s(u"All tags"));
    QCOMPARE(pictures->text(), s(u"Embedded pictures"));

    // A new panel removes nothing, and says so.
    QCOMPARE(ticked(fields), QStringList());
    QVERIFY(!all->isChecked() && !pictures->isChecked());
    QCOMPARE(panel->problem(), s(u"Choose what to remove"));
    QCOMPARE(need<QLabel>(d, u"summaryLabel")->text(), s(u"Choose what to remove"));
    QCOMPARE(table->rowCount(), 0);
    for (const TagChanges &c : panel->changes(tracks))
        QVERIFY(c.isEmpty());

    QSignalSpy changed(panel, &ToolPanel::optionsChanged);
    tick(fields, s(u"Album"), true);
    QCOMPARE(changed.size(), 1);
    QCOMPARE(panel->problem(), QString());
    QCOMPARE(panel->options().fields, QStringList({ s(u"Album") }));
    QVERIFY(!panel->options().allTags && !panel->options().removePictures);
    QCOMPARE(table->rowCount(), 3);
    QCOMPARE(row(table, 0), QStringList({ s(u"01.flac"), s(u"Album"), s(u"the album"), QString() }));
    tick(fields, s(u"Title"), true);
    tick(fields, s(u"Composer"), true); // no track has it: nothing more
    QCOMPARE(panel->options().fields, ticked(fields)); // the ticked rows, in list order
    QCOMPARE(panel->options().fields.size(), 3);
    QCOMPARE(table->rowCount(), 6);
    QList<TagChanges> got = panel->changes(tracks);
    QCOMPARE(got.size(), 3);
    for (int i = 0; i < 3; ++i)
        QVERIFY(same(got.at(i), removeTags(tracks.at(i).tags, panel->options())));
    QVERIFY(!got.at(0).other.contains(s(u"MOOD"))); // a key outside the catalogue is not offered

    // The pictures alone.
    tick(fields, s(u"Album"), false);
    tick(fields, s(u"Title"), false);
    tick(fields, s(u"Composer"), false);
    QCOMPARE(panel->problem(), s(u"Choose what to remove"));
    pictures->setChecked(true);
    QCOMPARE(panel->problem(), QString());
    QVERIFY(panel->options().removePictures);
    QCOMPARE(table->rowCount(), 1);
    QCOMPARE(row(table, 0), QStringList({ s(u"01.flac"), s(u"Pictures"), s(u"2"), s(u"0") }));
    pictures->setChecked(false);

    // All tags: the list is switched off; the pictures stay unless their box says so.
    changed.clear();
    all->setChecked(true);
    QVERIFY(changed.size() >= 1);
    QVERIFY(panel->options().allTags);
    QVERIFY(!fields->isEnabled());
    QCOMPARE(panel->problem(), QString());
    QCOMPARE(table->rowCount(), 3 + 1 + 3 + 3); // three fields each, and the one other key
    got = panel->changes(tracks);
    QCOMPARE(got.at(0).other.value(s(u"MOOD"), { s(u"kept") }), QStringList());
    QVERIFY(!got.at(0).pictures.has_value());
    for (int i = 0; i < 3; ++i)
        QVERIFY(same(got.at(i), removeTags(tracks.at(i).tags, panel->options())));
    all->setChecked(false);
    QVERIFY(fields->isEnabled());
    QCOMPARE(panel->problem(), s(u"Choose what to remove"));
}

void TagToolPanelsTest::removePanelRemembersNothing()
{
    // TT-REMOVE: what was removed last time is never ticked the next time.
    QTemporaryDir dir;
    QSettings settings(dir.path() + s(u"/s.ini"), QSettings::IniFormat);
    {
        auto *panel = new RemoveTagsPanel;
        NEED_PANEL(panel);
        TagToolDialog d(panel, three(), &settings);
        Recorder rec;
        d.setSaver(rec.saver());
        need<QCheckBox>(*panel, u"allTagsBox")->setChecked(true);
        need<QCheckBox>(*panel, u"picturesBox")->setChecked(true);
        tick(need<QListWidget>(*panel, u"fieldList"), s(u"Album"), true);
        QVERIFY(d.apply());
        QCOMPARE(rec.paths.size(), 3);
    }
    settings.sync();
    for (const QString &key : settings.allKeys())
        QVERIFY2(!key.startsWith(s(u"tagtools/remove")), qPrintable(key));
    // Whatever a file holds, it is not read either.
    settings.setValue(s(u"tagtools/remove/allTags"), true);
    settings.setValue(s(u"tagtools/remove/pictures"), true);
    settings.setValue(s(u"tagtools/remove/fields"), QStringList { s(u"Album") });
    QSettings later(nextRun(settings), QSettings::IniFormat);
    {
        auto *panel = new RemoveTagsPanel;
        TagToolDialog d(panel, three(), &later);
        QCOMPARE(ticked(need<QListWidget>(*panel, u"fieldList")), QStringList());
        QVERIFY(!panel->options().allTags && !panel->options().removePictures);
        QCOMPARE(panel->problem(), s(u"Choose what to remove"));
    }
}

void TagToolPanelsTest::renumberPanel()
{
    // TT-RENUMBER
    QList<EditTarget> tracks = three();
    tracks[0].tags.fields.insert(s(u"Track#"), { s(u"1") });
    tracks[1].tags.fields.insert(s(u"Track#"), { s(u"5") });
    auto *panel = new RenumberPanel;
    NEED_PANEL(panel);
    TagToolDialog d(panel, tracks);
    QCOMPARE(d.windowTitle(), s(u"Renumber Tracks"));
    auto *start = need<QSpinBox>(*panel, u"startSpin");
    auto *digits = need<QSpinBox>(*panel, u"digitsSpin");
    auto *total = need<QCheckBox>(*panel, u"totalBox");
    auto *table = need<QTableWidget>(d, u"previewTable");
    QCOMPARE(total->text(), s(u"Also write the track count"));
    QCOMPARE(start->minimum(), 0);
    QCOMPARE(start->maximum(), 99999);
    QCOMPARE(digits->minimum(), 0);
    QCOMPARE(digits->maximum(), 9);
    QCOMPARE(start->value(), 1);
    QCOMPARE(digits->value(), 0);
    QVERIFY(!total->isChecked());
    QCOMPARE(panel->options().start, 1);
    QCOMPARE(panel->options().digits, 0);
    QVERIFY(!panel->options().writeTotal);
    QCOMPARE(panel->problem(), QString());

    // In the order given: the first is 1 already, the second and third change.
    QCOMPARE(table->rowCount(), 2);
    QCOMPARE(row(table, 0), QStringList({ s(u"02.flac"), s(u"Track#"), s(u"5"), s(u"2") }));
    QCOMPARE(row(table, 1), QStringList({ s(u"03.flac"), s(u"Track#"), QString(), s(u"3") }));

    QSignalSpy changed(panel, &ToolPanel::optionsChanged);
    digits->setValue(2);
    QCOMPARE(changed.size(), 1);
    QCOMPARE(panel->options().digits, 2);
    QCOMPARE(table->rowCount(), 3);
    QCOMPARE(row(table, 0), QStringList({ s(u"01.flac"), s(u"Track#"), s(u"1"), s(u"01") }));
    total->setChecked(true);
    QCOMPARE(changed.size(), 2);
    QVERIFY(panel->options().writeTotal);
    QCOMPARE(table->rowCount(), 6);
    QCOMPARE(row(table, 0), QStringList({ s(u"01.flac"), s(u"Track Count"), QString(), s(u"3") }));
    start->setValue(10);
    QCOMPARE(changed.size(), 3);
    QCOMPARE(panel->options().start, 10);
    QCOMPARE(row(table, 5), QStringList({ s(u"03.flac"), s(u"Track#"), QString(), s(u"12") }));

    // The panel's answers are the core's, for the tracks in the order given.
    QList<TrackTags> tags;
    for (const EditTarget &t : tracks)
        tags.append(t.tags);
    const QList<TagChanges> want = renumberTracks(tags, panel->options());
    const QList<TagChanges> got = panel->changes(tracks);
    QCOMPARE(got.size(), 3);
    QCOMPARE(want.size(), 3);
    for (int i = 0; i < 3; ++i)
        QVERIFY(same(got.at(i), want.at(i)));
    // The other way round: other numbers.
    std::reverse(tracks.begin(), tracks.end());
    QCOMPARE(panel->changes(tracks).at(0).fields.value(s(u"Track#")), QStringList({ s(u"10") }));
    QCOMPARE(panel->changes({ }).size(), 0);
}

void TagToolPanelsTest::renumberPanelRemembers()
{
    // TT-RENUMBER: the digits and the box are remembered; the start is not.
    QTemporaryDir dir;
    QSettings settings(dir.path() + s(u"/s.ini"), QSettings::IniFormat);
    {
        auto *panel = new RenumberPanel;
        NEED_PANEL(panel);
        TagToolDialog d(panel, three(), &settings);
        Recorder rec;
        d.setSaver(rec.saver());
        need<QSpinBox>(*panel, u"startSpin")->setValue(10);
        need<QSpinBox>(*panel, u"digitsSpin")->setValue(3);
        need<QCheckBox>(*panel, u"totalBox")->setChecked(true);
        QVERIFY(d.apply());
    }
    QSettings later(nextRun(settings), QSettings::IniFormat);
    QCOMPARE(later.value(s(u"tagtools/renumber/digits")).toString(), s(u"3"));
    QCOMPARE(later.value(s(u"tagtools/renumber/total")).toString(), s(u"true"));
    QVERIFY(!later.contains(s(u"tagtools/renumber/start")));
    {
        auto *panel = new RenumberPanel;
        TagToolDialog d(panel, three(), &later);
        QCOMPARE(panel->options().start, 1);
        QCOMPARE(panel->options().digits, 3);
        QVERIFY(panel->options().writeTotal);
        // Previewed with the loaded options: 001, 002, 003 and the count.
        QCOMPARE(need<QTableWidget>(d, u"previewTable")->rowCount(), 6);
    }
    for (const char16_t *odd : { u"12", u"-1", u"many", u"" }) {
        settings.setValue(s(u"tagtools/renumber/digits"), s(odd));
        settings.setValue(s(u"tagtools/renumber/total"), s(u"perhaps"));
        settings.setValue(s(u"tagtools/renumber/start"), 40);
        QSettings strange(nextRun(settings), QSettings::IniFormat);
        auto *panel = new RenumberPanel;
        TagToolDialog d(panel, three(), &strange);
        QCOMPARE(panel->options().digits, 0);
        QVERIFY(!panel->options().writeTotal);
        QCOMPARE(panel->options().start, 1);
    }
}

void TagToolPanelsTest::inferPanel()
{
    // TT-INFER
    EditTarget a = at(21, s(u"/music/The Band/Great Album/03 - Some Song.flac"));
    a.tags.fields.insert(s(u"Title"), { s(u"old title") });
    const EditTarget b = at(22, s(u"/music/The Band/Great Album/07 - Other.flac"));
    const EditTarget c = at(23, s(u"/music/The Band/Great Album/loose.flac"));
    const QList<EditTarget> tracks { a, b, c };
    auto *panel = new InferPanel;
    NEED_PANEL(panel);
    TagToolDialog d(panel, tracks);
    QCOMPARE(d.windowTitle(), s(u"Infer Tags From Filename"));
    auto *pattern = need<QLineEdit>(*panel, u"patternEdit");
    auto *blank = need<QCheckBox>(*panel, u"blankOnlyBox");
    auto *table = need<QTableWidget>(d, u"previewTable");
    QCOMPARE(blank->text(), s(u"Only fill fields that are empty"));
    QCOMPARE(pattern->text(), s(u"<Track#> - <Title>"));
    QCOMPARE(panel->pattern(), s(u"<Track#> - <Title>"));
    QVERIFY(blank->isChecked());
    QVERIFY(panel->blankFieldsOnly());
    QCOMPARE(panel->problem(), QString());

    // Empty fields only: the first track keeps its title; the third name does not fit.
    QCOMPARE(table->rowCount(), 3);
    QCOMPARE(row(table, 0), QStringList({ s(u"03 - Some Song.flac"), s(u"Track#"), QString(), s(u"03") }));
    QCOMPARE(row(table, 1), QStringList({ s(u"07 - Other.flac"), s(u"Title"), QString(), s(u"Other") }));
    QCOMPARE(need<QLabel>(d, u"summaryLabel")->text(), s(u"2 of 3 tracks would change"));

    QSignalSpy changed(panel, &ToolPanel::optionsChanged);
    blank->setChecked(false);
    QCOMPARE(changed.size(), 1);
    QVERIFY(!panel->blankFieldsOnly());
    QCOMPARE(table->rowCount(), 4);
    QCOMPARE(row(table, 0), QStringList({ s(u"03 - Some Song.flac"), s(u"Title"), s(u"old title"), s(u"Some Song") }));

    pattern->setText(s(u"<Album>/<Track#> - <Title>"));
    QCOMPARE(changed.size(), 2);
    QCOMPARE(panel->pattern(), s(u"<Album>/<Track#> - <Title>"));
    QCOMPARE(table->rowCount(), 6);
    QCOMPARE(row(table, 0), QStringList({ s(u"03 - Some Song.flac"), s(u"Album"), QString(), s(u"Great Album") }));
    QList<TagChanges> got = panel->changes(tracks);
    QCOMPARE(got.size(), 3);
    for (int i = 0; i < 3; ++i)
        QVERIFY(same(got.at(i), inferTags(tracks.at(i).tags, tracks.at(i).path, panel->pattern(), false)));
    QVERIFY(got.at(2).isEmpty());

    // A pattern that cannot be used: the core's own words, and nothing to apply.
    for (const char16_t *bad : { u"", u"<Nonsense>", u"<Title", u"<Title><Artist>", u"no field" }) {
        pattern->setText(s(bad));
        QVERIFY2(!validateInferPattern(s(bad)).isEmpty(), qPrintable(s(bad)));
        QCOMPARE(panel->problem(), validateInferPattern(s(bad)));
        QCOMPARE(need<QLabel>(d, u"summaryLabel")->text(), panel->problem());
        QCOMPARE(table->rowCount(), 0);
        for (const TagChanges &change : panel->changes(tracks))
            QVERIFY(change.isEmpty());
    }
    pattern->setText(s(u"<Title>"));
    QCOMPARE(panel->problem(), QString());
    QCOMPARE(table->rowCount(), 3);
}

void TagToolPanelsTest::inferPanelRemembers()
{
    // TT-INFER: the pattern and the box are remembered.
    QTemporaryDir dir;
    QSettings settings(dir.path() + s(u"/s.ini"), QSettings::IniFormat);
    const QList<EditTarget> tracks { at(31, s(u"/music/A Band/An Album/02 - A Song.flac")) };
    {
        auto *panel = new InferPanel;
        NEED_PANEL(panel);
        TagToolDialog d(panel, tracks, &settings);
        Recorder rec;
        d.setSaver(rec.saver());
        need<QLineEdit>(*panel, u"patternEdit")->setText(s(u"<Album Artist>/<Album>/<Track#> - <Title>"));
        need<QCheckBox>(*panel, u"blankOnlyBox")->setChecked(false);
        QVERIFY(d.apply());
        QCOMPARE(rec.changes.size(), 1);
        QCOMPARE(rec.changes.at(0).fields.value(s(u"Album Artist")), QStringList({ s(u"A Band") }));
    }
    QSettings later(nextRun(settings), QSettings::IniFormat);
    QCOMPARE(later.value(s(u"tagtools/infer/pattern")).toString(), s(u"<Album Artist>/<Album>/<Track#> - <Title>"));
    QCOMPARE(later.value(s(u"tagtools/infer/blankOnly")).toString(), s(u"false"));
    {
        auto *panel = new InferPanel;
        TagToolDialog d(panel, tracks, &later);
        QCOMPARE(panel->pattern(), s(u"<Album Artist>/<Album>/<Track#> - <Title>"));
        QVERIFY(!panel->blankFieldsOnly());
        QCOMPARE(need<QTableWidget>(d, u"previewTable")->rowCount(), 4); // previewed with the loaded options
    }
    // Nothing usable stored: the pattern a new panel has, and the careful box.
    settings.setValue(s(u"tagtools/infer/pattern"), s(u"   "));
    settings.setValue(s(u"tagtools/infer/blankOnly"), s(u"perhaps"));
    QSettings odd(nextRun(settings), QSettings::IniFormat);
    {
        auto *panel = new InferPanel;
        TagToolDialog d(panel, tracks, &odd);
        QCOMPARE(panel->pattern(), s(u"<Track#> - <Title>"));
        QVERIFY(panel->blankFieldsOnly());
    }
    // A stored pattern that cannot be used is shown as it is, with its problem.
    settings.setValue(s(u"tagtools/infer/pattern"), s(u"<Nonsense>"));
    QSettings bad(nextRun(settings), QSettings::IniFormat);
    {
        auto *panel = new InferPanel;
        TagToolDialog d(panel, tracks, &bad);
        QCOMPARE(panel->pattern(), s(u"<Nonsense>"));
        QCOMPARE(panel->problem(), validateInferPattern(s(u"<Nonsense>")));
    }
}

QTEST_MAIN(TagToolPanelsTest)
#include "tst_ui_tagtools_panels.moc"
