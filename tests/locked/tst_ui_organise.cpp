// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/organise-dialog.md (OD-x). Runs on
// the offscreen platform. Most tests replace the planner and the executor
// with recorders; the last one moves a real file inside a temporary folder.
#include <QFile>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest>

#include "organise/organisedialog.h"

using namespace linuxbee;
using linuxbee::ui::OrganiseDialog;

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

// The named child, or (failing the test) a stand-in, so that a missing widget
// is a failure and not a crash.
template <typename T> T *need(QWidget &d, const char16_t *name)
{
    if (T *found = d.findChild<T *>(s(name)))
        return found;
    QTest::qFail(qPrintable(QStringLiteral("no widget named ") + s(name)), __FILE__, __LINE__);
    static T standIn;
    return &standIn;
}

// A planner that needs no files: the name of each "file" says what becomes of it.
struct Fake {
    int planCalls = 0;
    int runCalls = 0;
    OrganiseOptions lastPlanOptions;
    OrganiseOptions lastRunOptions;
    QList<PlannedMove> lastRunPlan;
    int failIndex = -1;
    int stopAt = -1; // the executor asks for progress and stops when told to
    QList<int> progressSeen;

    OrganiseDialog::Planner planner()
    {
        return [this](const QStringList &files, const OrganiseOptions &o) {
            ++planCalls;
            lastPlanOptions = o;
            QList<PlannedMove> out;
            for (const QString &f : files) {
                PlannedMove m;
                m.source = f;
                const QString name = f.section(QLatin1Char('/'), -1);
                if (name.startsWith(s(u"untagged")) && o.pathRules.skipWhenTagsMissing) {
                    m.status = PlannedMove::Status::MissingTags;
                    m.missingFields = { s(u"Album"), s(u"Title") };
                } else if (name.startsWith(s(u"inplace"))) {
                    m.status = PlannedMove::Status::AlreadyInPlace;
                    m.target = f;
                } else if (name.startsWith(s(u"gone"))) {
                    m.status = PlannedMove::Status::NotFound;
                } else {
                    m.status = PlannedMove::Status::Ready;
                    m.target = o.targetRoot + s(u"/sorted/") + name;
                    m.replaces = name.startsWith(s(u"clash")) && o.replaceExisting;
                    m.renamed = name.startsWith(s(u"clash")) && !o.replaceExisting;
                }
                out.append(m);
            }
            return out;
        };
    }
    OrganiseDialog::Executor executor()
    {
        return [this](const QList<PlannedMove> &plan, const OrganiseOptions &o, const OrganiseProgress &progress) {
            ++runCalls;
            lastRunPlan = plan;
            lastRunOptions = o;
            OrganiseReport r;
            for (int i = 0; i < plan.size(); ++i) {
                if (progress && !progress(i, int(plan.size()))) {
                    r.cancelled = true;
                    break;
                }
                progressSeen.append(i);
                MoveOutcome out;
                out.source = plan.at(i).source;
                out.target = plan.at(i).target;
                if (plan.at(i).status != PlannedMove::Status::Ready) {
                    out.status = MoveOutcome::Status::Skipped;
                    out.message = s(u"not ready");
                    ++r.skipped;
                } else if (i == failIndex) {
                    out.status = MoveOutcome::Status::Failed;
                    out.message = s(u"disk full");
                    ++r.failed;
                } else {
                    out.status = MoveOutcome::Status::Done;
                    ++r.done;
                }
                r.outcomes.append(out);
            }
            return r;
        };
    }
};

const QStringList kFiles = { s(u"/in/a.flac"), s(u"/in/clash.flac"), s(u"/in/untagged.mp3"), s(u"/in/inplace.flac"), s(u"/in/gone.flac") };

struct Fixture {
    QTemporaryDir dir;
    QSettings settings;
    Fake fake;
    Fixture()
        : settings(dir.path() + QStringLiteral("/t.ini"), QSettings::IniFormat)
    {
    }
    void attach(OrganiseDialog &d)
    {
        d.setPlanner(fake.planner());
        d.setExecutor(fake.executor());
    }
};

QStringList column(QTreeWidget *table, int c)
{
    QStringList out;
    for (int i = 0; i < table->topLevelItemCount(); ++i)
        out.append(table->topLevelItem(i)->text(c));
    return out;
}

}

class OrganiseDialogTest : public QObject {
    Q_OBJECT
private slots:
    void structure();
    void defaultsAndOptions();
    void previewTable();
    void previewFollowsOptions();
    void invalidTemplate();
    void run();
    void runWithFailure();
    void stop();
    void remembered();
    void realFile();
};

void OrganiseDialogTest::structure()
{
    Fixture f;
    OrganiseDialog d(kFiles, &f.settings);
    f.attach(d);
    QCOMPARE(d.objectName(), s(u"organiseDialog"));
    QVERIFY(!d.windowTitle().isEmpty());
    for (const char16_t *n : { u"targetEdit", u"templateEdit" })
        QVERIFY2(d.findChild<QLineEdit *>(s(n)), qPrintable(s(n)));
    for (const char16_t *n : { u"moveRadio", u"copyRadio" })
        QVERIFY2(d.findChild<QRadioButton *>(s(n)), qPrintable(s(n)));
    for (const char16_t *n : { u"replaceCheck", u"otherFilesCheck", u"removeEmptyCheck", u"skipMissingCheck" })
        QVERIFY2(d.findChild<QCheckBox *>(s(n)), qPrintable(s(n)));
    for (const char16_t *n : { u"browseButton", u"runButton", u"closeButton" })
        QVERIFY2(d.findChild<QPushButton *>(s(n)), qPrintable(s(n)));
    for (const char16_t *n : { u"problemLabel", u"summaryLabel" })
        QVERIFY2(d.findChild<QLabel *>(s(n)), qPrintable(s(n)));
    QVERIFY(d.findChild<QProgressBar *>(s(u"progressBar")));
    auto *table = d.findChild<QTreeWidget *>(s(u"previewTable"));
    QVERIFY(table);
    QCOMPARE(table->columnCount(), 3);
    QCOMPARE(table->headerItem()->text(0), s(u"From"));
    QCOMPARE(table->headerItem()->text(1), s(u"To"));
    QCOMPARE(table->headerItem()->text(2), s(u"Status"));
    d.resize(800, 600);
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));
    QVERIFY(!need<QProgressBar>(d, u"progressBar")->isVisible()); // only while running
    // Options above the preview, the buttons below it.
    const int tableY = table->mapTo(&d, QPoint(0, 0)).y();
    QVERIFY(need<QLineEdit>(d, u"templateEdit")->mapTo(&d, QPoint(0, 0)).y() < tableY);
    QVERIFY(need<QPushButton>(d, u"runButton")->mapTo(&d, QPoint(0, 0)).y() > tableY + table->height() - 1);
    QVERIFY(table->height() >= 120);
}

void OrganiseDialogTest::defaultsAndOptions()
{
    // OD-OPTIONS: with nothing stored, the controls show the organiser's defaults.
    Fixture f;
    OrganiseDialog d(kFiles, &f.settings);
    f.attach(d);
    QCOMPARE(need<QLineEdit>(d, u"targetEdit")->text(), QString());
    QCOMPARE(d.templateText(), s(u"<Album Artist>\\<Album>\\<Disc-Track#> <Title>"));
    QCOMPARE(need<QLineEdit>(d, u"templateEdit")->text(), d.templateText());
    QVERIFY(need<QRadioButton>(d, u"moveRadio")->isChecked());
    QVERIFY(!need<QRadioButton>(d, u"copyRadio")->isChecked());
    QVERIFY(!need<QCheckBox>(d, u"replaceCheck")->isChecked());
    QVERIFY(!need<QCheckBox>(d, u"otherFilesCheck")->isChecked());
    QVERIFY(!need<QCheckBox>(d, u"removeEmptyCheck")->isChecked()); // OD-OPTIONS: emptied folders stay
    QVERIFY(need<QCheckBox>(d, u"skipMissingCheck")->isChecked());
    OrganiseOptions o = d.options();
    QCOMPARE(o.targetRoot, QString());
    QVERIFY(o.useTemplate);
    QVERIFY(o.namingTemplate.isValid());
    QCOMPARE(o.namingTemplate.referencedFields(), QStringList({ s(u"Album Artist"), s(u"Album"), s(u"Disc-Track#"), s(u"Title") }));
    QVERIFY(o.mode == OrganiseOptions::Mode::Move);
    QVERIFY(!o.replaceExisting && !o.moveOtherFiles && !o.removeEmptyFolders && o.pathRules.skipWhenTagsMissing);
    // Every control reaches options().
    need<QLineEdit>(d, u"targetEdit")->setText(s(u" /music/sorted/ "));
    need<QLineEdit>(d, u"templateEdit")->setText(s(u"<Artist> - <Title>"));
    need<QCheckBox>(d, u"replaceCheck")->setChecked(true);
    need<QCheckBox>(d, u"otherFilesCheck")->setChecked(true);
    need<QCheckBox>(d, u"removeEmptyCheck")->setChecked(true);
    need<QCheckBox>(d, u"skipMissingCheck")->setChecked(false);
    o = d.options();
    QCOMPARE(o.targetRoot, s(u"/music/sorted")); // trimmed and cleaned
    QCOMPARE(o.namingTemplate.referencedFields(), QStringList({ s(u"Artist"), s(u"Title") }));
    QVERIFY(o.replaceExisting && o.moveOtherFiles && o.removeEmptyFolders && !o.pathRules.skipWhenTagsMissing);
    // Copying: the two "move only" options are greyed out and off in options(), but keep their ticks.
    need<QRadioButton>(d, u"copyRadio")->setChecked(true);
    o = d.options();
    QVERIFY(o.mode == OrganiseOptions::Mode::Copy);
    QVERIFY(!o.moveOtherFiles && !o.removeEmptyFolders);
    QVERIFY(!need<QCheckBox>(d, u"otherFilesCheck")->isEnabled());
    QVERIFY(!need<QCheckBox>(d, u"removeEmptyCheck")->isEnabled());
    QVERIFY(need<QCheckBox>(d, u"otherFilesCheck")->isChecked());
    need<QRadioButton>(d, u"moveRadio")->setChecked(true);
    QVERIFY(need<QCheckBox>(d, u"otherFilesCheck")->isEnabled());
    QVERIFY(d.options().moveOtherFiles);
}

void OrganiseDialogTest::previewTable()
{
    // OD-PREVIEW
    Fixture f;
    f.settings.setValue(s(u"organise/target"), s(u"/out"));
    OrganiseDialog d(kFiles, &f.settings);
    f.attach(d);
    auto *table = need<QTreeWidget>(d, u"previewTable");
    QCOMPARE(d.preview().size(), 5);
    QCOMPARE(column(table, 0), kFiles);
    QCOMPARE(column(table, 1), QStringList({ s(u"/out/sorted/a.flac"), s(u"/out/sorted/clash.flac"), s(u""), s(u"/in/inplace.flac"), s(u"") }));
    QCOMPARE(column(table, 2),
        QStringList({ s(u"Ready"), s(u"Renamed"), s(u"Missing: Album, Title"), s(u"Already in place"), s(u"File not found") }));
    QCOMPARE(need<QLabel>(d, u"summaryLabel")->text(), s(u"2 to move, 1 already in place, 2 cannot be done"));
    QVERIFY(need<QPushButton>(d, u"runButton")->isEnabled());
    QVERIFY(need<QLabel>(d, u"problemLabel")->text().isEmpty());
    QCOMPARE(f.fake.runCalls, 0); // a preview never moves anything
}

void OrganiseDialogTest::previewFollowsOptions()
{
    Fixture f;
    OrganiseDialog d(kFiles, &f.settings);
    f.attach(d);
    auto *table = need<QTreeWidget>(d, u"previewTable");
    int calls = f.fake.planCalls;
    need<QLineEdit>(d, u"targetEdit")->setText(s(u"/elsewhere"));
    QVERIFY(f.fake.planCalls > calls);
    QCOMPARE(f.fake.lastPlanOptions.targetRoot, s(u"/elsewhere"));
    QCOMPARE(column(table, 1).first(), s(u"/elsewhere/sorted/a.flac"));
    need<QCheckBox>(d, u"replaceCheck")->setChecked(true);
    QCOMPARE(column(table, 2).at(1), s(u"Replaces a file"));
    need<QCheckBox>(d, u"skipMissingCheck")->setChecked(false);
    QCOMPARE(column(table, 2).at(2), s(u"Ready"));
    QCOMPARE(need<QLabel>(d, u"summaryLabel")->text(), s(u"3 to move, 1 already in place, 1 cannot be done"));
    need<QRadioButton>(d, u"copyRadio")->setChecked(true);
    QVERIFY(f.fake.lastPlanOptions.mode == OrganiseOptions::Mode::Copy);
    QCOMPARE(need<QLabel>(d, u"summaryLabel")->text(), s(u"3 to copy, 1 already in place, 1 cannot be done"));
    calls = f.fake.planCalls;
    need<QLineEdit>(d, u"templateEdit")->setText(s(u"<Artist>\\<Title>"));
    QVERIFY(f.fake.planCalls > calls);
    QCOMPARE(f.fake.lastPlanOptions.namingTemplate.referencedFields(), QStringList({ s(u"Artist"), s(u"Title") }));
    // Nothing ready: nothing to proceed with.
    OrganiseDialog none({ s(u"/in/inplace.flac"), s(u"/in/gone.flac") }, &f.settings);
    f.attach(none);
    QVERIFY(!need<QPushButton>(none, u"runButton")->isEnabled());
    QVERIFY(!none.run());
    QCOMPARE(f.fake.runCalls, 0);
    OrganiseDialog empty({ }, &f.settings);
    f.attach(empty);
    QCOMPARE(need<QLabel>(empty, u"summaryLabel")->text(), s(u"0 to move, 0 already in place, 0 cannot be done"));
    QVERIFY(!need<QPushButton>(empty, u"runButton")->isEnabled());
}

void OrganiseDialogTest::invalidTemplate()
{
    // OD-TEMPLATE
    Fixture f;
    OrganiseDialog d(kFiles, &f.settings);
    f.attach(d);
    const int calls = f.fake.planCalls;
    need<QLineEdit>(d, u"templateEdit")->setText(s(u"<Artist>\\$Left(<Title>"));
    const Template bad = Template::parse(s(u"<Artist>\\$Left(<Title>"));
    QVERIFY(!bad.isValid());
    QCOMPARE(need<QLabel>(d, u"problemLabel")->text(),
        QStringLiteral("Template: %1 (at character %2)").arg(bad.error().message).arg(bad.error().position + 1));
    QCOMPARE(f.fake.planCalls, calls); // the planner is not asked
    QCOMPARE(need<QTreeWidget>(d, u"previewTable")->topLevelItemCount(), 0);
    QVERIFY(d.preview().isEmpty());
    QVERIFY(need<QLabel>(d, u"summaryLabel")->text().isEmpty());
    QVERIFY(!need<QPushButton>(d, u"runButton")->isEnabled());
    QVERIFY(!d.run());
    QCOMPARE(f.fake.runCalls, 0);
    need<QLineEdit>(d, u"templateEdit")->setText(s(u"<Title>"));
    QVERIFY(need<QLabel>(d, u"problemLabel")->text().isEmpty());
    QCOMPARE(need<QTreeWidget>(d, u"previewTable")->topLevelItemCount(), 5);
    QVERIFY(need<QPushButton>(d, u"runButton")->isEnabled());
}

void OrganiseDialogTest::run()
{
    // OD-RUN
    Fixture f;
    f.settings.setValue(s(u"organise/target"), s(u"/out"));
    OrganiseDialog d(kFiles, &f.settings);
    f.attach(d);
    qRegisterMetaType<linuxbee::OrganiseReport>();
    QSignalSpy organised(&d, &OrganiseDialog::organised);
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));
    QTest::mouseClick(need<QPushButton>(d, u"runButton"), Qt::LeftButton);
    QCOMPARE(f.fake.runCalls, 1);
    QCOMPARE(f.fake.lastRunPlan.size(), 5); // the plan shown is the plan run, entry for entry
    QCOMPARE(f.fake.lastRunPlan.at(1).target, s(u"/out/sorted/clash.flac"));
    QCOMPARE(f.fake.lastRunOptions.targetRoot, s(u"/out"));
    QCOMPARE(f.fake.progressSeen, QList<int>({ 0, 1, 2, 3, 4 }));
    QCOMPARE(organised.size(), 1);
    const auto report = organised.at(0).at(0).value<linuxbee::OrganiseReport>();
    QCOMPARE(report.done, 2);
    QCOMPARE(report.skipped, 3);
    // All went well: the dialog closes.
    QVERIFY(!d.isVisible());
    QCOMPARE(d.result(), int(QDialog::Accepted));
}

void OrganiseDialogTest::runWithFailure()
{
    Fixture f;
    f.settings.setValue(s(u"organise/target"), s(u"/out"));
    f.fake.failIndex = 1;
    OrganiseDialog d(kFiles, &f.settings);
    f.attach(d);
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));
    QVERIFY(d.run());
    // Something failed: the dialog stays, and says what happened to each file.
    QVERIFY(d.isVisible());
    QCOMPARE(column(need<QTreeWidget>(d, u"previewTable"), 2),
        QStringList({ s(u"Done"), s(u"Failed: disk full"), s(u"Skipped: not ready"), s(u"Skipped: not ready"), s(u"Skipped: not ready") }));
    QCOMPARE(need<QLabel>(d, u"summaryLabel")->text(), s(u"1 done, 3 skipped, 1 failed"));
    QVERIFY(!need<QProgressBar>(d, u"progressBar")->isVisible());
    // The table now shows results, not a plan: Proceed is off until an option changes.
    QVERIFY(!need<QPushButton>(d, u"runButton")->isEnabled());
    QVERIFY(!d.run());
    QCOMPARE(f.fake.runCalls, 1);
    need<QCheckBox>(d, u"replaceCheck")->setChecked(true); // a fresh preview
    QVERIFY(need<QPushButton>(d, u"runButton")->isEnabled());
    QCOMPARE(column(need<QTreeWidget>(d, u"previewTable"), 2).first(), s(u"Ready"));
}

void OrganiseDialogTest::stop()
{
    // OD-RUN: while it runs, Close asks the run to stop.
    Fixture f;
    f.settings.setValue(s(u"organise/target"), s(u"/out"));
    OrganiseDialog d(kFiles, &f.settings);
    Fake &fake = f.fake;
    d.setPlanner(fake.planner());
    QPushButton *close = need<QPushButton>(d, u"closeButton");
    QProgressBar *bar = need<QProgressBar>(d, u"progressBar");
    bool barSeen = false;
    int barValue = -1;
    d.setExecutor([&](const QList<PlannedMove> &plan, const OrganiseOptions &, const OrganiseProgress &progress) {
        OrganiseReport r;
        for (int i = 0; i < plan.size(); ++i) {
            if (i == 2) { // the user presses Close while the third file is next
                barSeen = bar->isVisible();
                barValue = bar->value();
                QTest::mouseClick(close, Qt::LeftButton);
            }
            if (!progress(i, int(plan.size()))) {
                r.cancelled = true;
                break;
            }
            ++r.done;
        }
        return r;
    });
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));
    QVERIFY(d.run());
    QVERIFY(barSeen);
    QCOMPARE(barValue, 1); // it shows the index last reported
    QCOMPARE(bar->maximum(), 5);
    QVERIFY(d.isVisible()); // stopped, not closed
    QCOMPARE(need<QLabel>(d, u"summaryLabel")->text(), s(u"2 done, 0 skipped, 0 failed (stopped)"));
    // Not running any more: Close closes.
    QTest::mouseClick(close, Qt::LeftButton);
    QVERIFY(!d.isVisible());
    QCOMPARE(d.result(), int(QDialog::Rejected));
}

void OrganiseDialogTest::remembered()
{
    // OD-OPTIONS: what was used for a run is there next time; what was only looked at is not.
    Fixture f;
    {
        OrganiseDialog d(kFiles, &f.settings);
        f.attach(d);
        need<QLineEdit>(d, u"targetEdit")->setText(s(u"/looked/at"));
        need<QPushButton>(d, u"closeButton")->click();
    }
    QVERIFY(!f.settings.contains(s(u"organise/target")));
    {
        OrganiseDialog d(kFiles, &f.settings);
        f.attach(d);
        need<QLineEdit>(d, u"targetEdit")->setText(s(u"/music/sorted/"));
        need<QLineEdit>(d, u"templateEdit")->setText(s(u"<Artist>\\<Title>"));
        need<QRadioButton>(d, u"copyRadio")->setChecked(true);
        need<QCheckBox>(d, u"replaceCheck")->setChecked(true);
        need<QCheckBox>(d, u"skipMissingCheck")->setChecked(false);
        need<QCheckBox>(d, u"removeEmptyCheck")->setChecked(true); // kept, though Copy counts it as off
        QVERIFY(d.run());
    }
    f.settings.sync();
    QCOMPARE(f.settings.value(s(u"organise/target")).toString(), s(u"/music/sorted"));
    QCOMPARE(f.settings.value(s(u"organise/template")).toString(), s(u"<Artist>\\<Title>"));
    QSettings later(nextRun(f.settings), QSettings::IniFormat); // as the next run reads it
    OrganiseDialog d(kFiles, &later);
    f.attach(d);
    QCOMPARE(need<QLineEdit>(d, u"targetEdit")->text(), s(u"/music/sorted"));
    QCOMPARE(d.templateText(), s(u"<Artist>\\<Title>"));
    QVERIFY(need<QRadioButton>(d, u"copyRadio")->isChecked());
    QVERIFY(need<QCheckBox>(d, u"replaceCheck")->isChecked());
    QVERIFY(!need<QCheckBox>(d, u"skipMissingCheck")->isChecked());
    QVERIFY(need<QCheckBox>(d, u"removeEmptyCheck")->isChecked());
    QVERIFY(d.options().mode == OrganiseOptions::Mode::Copy);
}

void OrganiseDialogTest::realFile()
{
    // With the planner and executor left alone, the dialog uses the organiser.
    QTemporaryDir dir;
    QSettings settings(dir.path() + s(u"/t.ini"), QSettings::IniFormat);
    QVERIFY(QDir(dir.path()).mkpath(s(u"in")));
    const QString source = dir.path() + s(u"/in/basic.flac");
    QVERIFY(QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/basic.flac"), source));
    OrganiseDialog d({ source }, &settings);
    need<QLineEdit>(d, u"targetEdit")->setText(dir.path() + s(u"/out"));
    need<QLineEdit>(d, u"templateEdit")->setText(s(u"moved\\song"));
    const QString target = dir.path() + s(u"/out/moved/song.flac");
    QCOMPARE(d.preview().size(), 1);
    QVERIFY(d.preview().at(0).status == PlannedMove::Status::Ready);
    QCOMPARE(d.preview().at(0).target, target);
    QVERIFY(QFile::exists(source)); // the preview moved nothing
    QVERIFY(d.run());
    QVERIFY(QFile::exists(target));
    QVERIFY(!QFile::exists(source));
    // The folder the track was in is the only place a track left, so it is
    // the floor (organise.md, O-EMPTY): empty now, and still there.
    QVERIFY(QDir(dir.path() + s(u"/in")).exists());
    QVERIFY(QDir(dir.path() + s(u"/in")).isEmpty());
    QCOMPARE(d.result(), int(QDialog::Accepted));
}

QTEST_MAIN(OrganiseDialogTest)
#include "tst_ui_organise.moc"
