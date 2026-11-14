// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/organise-dialog.md (OD-RUN), the
// parts added after the review of plan 13: the plan shown is the plan run;
// a run cannot be left by closing the window; rows the run never reached.
// Runs on the offscreen platform, with recorders for planner and executor.
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

class OrganiseDialogReviewTest : public QObject {
    Q_OBJECT
private slots:
    void runsThePlanShown();
    void escapeDuringARun();
    void windowCloseDuringARun();
    void notReached();
};

namespace {

// An executor that does `act` when the third file is next, then goes on as
// the dialog's progress answer says.
OrganiseDialog::Executor actingAtTheThird(std::function<void()> act, int *reached)
{
    return [act, reached](const QList<PlannedMove> &plan, const OrganiseOptions &, const OrganiseProgress &progress) {
        OrganiseReport r;
        for (int i = 0; i < plan.size(); ++i) {
            if (i == 2)
                act();
            if (!progress(i, int(plan.size()))) {
                r.cancelled = true;
                break;
            }
            MoveOutcome out;
            out.source = plan.at(i).source;
            out.target = plan.at(i).target;
            out.status = MoveOutcome::Status::Done;
            r.outcomes.append(out);
            ++r.done;
            *reached = i;
        }
        return r;
    };
}

}

void OrganiseDialogReviewTest::runsThePlanShown()
{
    // OD-RUN 3: the plan shown, not one worked out again.
    Fixture f;
    f.settings.setValue(s(u"organise/target"), s(u"/out"));
    OrganiseDialog d(kFiles, &f.settings);
    f.attach(d);
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));
    const QList<PlannedMove> shown = d.preview();
    QCOMPARE(shown.size(), 5);
    const int plannedBefore = f.fake.planCalls;
    QVERIFY(plannedBefore >= 1);
    QTest::mouseClick(need<QPushButton>(d, u"runButton"), Qt::LeftButton);
    QCOMPARE(f.fake.runCalls, 1);
    QCOMPARE(f.fake.planCalls, plannedBefore); // the planner was not asked again
    QCOMPARE(f.fake.lastRunPlan.size(), shown.size());
    for (int i = 0; i < shown.size(); ++i) {
        QCOMPARE(f.fake.lastRunPlan.at(i).source, shown.at(i).source);
        QCOMPARE(f.fake.lastRunPlan.at(i).target, shown.at(i).target);
        QCOMPARE(f.fake.lastRunPlan.at(i).status, shown.at(i).status);
    }
}

void OrganiseDialogReviewTest::escapeDuringARun()
{
    // OD-RUN: Escape during a run asks it to stop; the dialog stays.
    Fixture f;
    f.settings.setValue(s(u"organise/target"), s(u"/out"));
    OrganiseDialog d(kFiles, &f.settings);
    d.setPlanner(f.fake.planner());
    int reached = -1;
    bool visibleRightAfter = false;
    d.setExecutor(actingAtTheThird(
        [&] {
            QTest::keyClick(&d, Qt::Key_Escape);
            visibleRightAfter = d.isVisible();
        },
        &reached));
    qRegisterMetaType<linuxbee::OrganiseReport>();
    QSignalSpy organised(&d, &OrganiseDialog::organised);
    QSignalSpy finished(&d, &QDialog::finished);
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));
    QVERIFY(d.run());
    QVERIFY2(visibleRightAfter, "Escape closed the dialog in the middle of a run");
    QCOMPARE(reached, 1); // the run stopped before the third file
    QVERIFY(d.isVisible());
    QCOMPARE(finished.size(), 0);
    QCOMPARE(organised.size(), 1);
    QVERIFY(organised.at(0).at(0).value<linuxbee::OrganiseReport>().cancelled);
    QCOMPARE(need<QLabel>(d, u"summaryLabel")->text(), s(u"2 done, 0 skipped, 0 failed (stopped)"));
    // No run under way: Escape closes, as rejected.
    QTest::keyClick(&d, Qt::Key_Escape);
    QVERIFY(!d.isVisible());
    QCOMPARE(d.result(), int(QDialog::Rejected));
}

void OrganiseDialogReviewTest::windowCloseDuringARun()
{
    // OD-RUN: so does the window's own close button.
    Fixture f;
    f.settings.setValue(s(u"organise/target"), s(u"/out"));
    OrganiseDialog d(kFiles, &f.settings);
    d.setPlanner(f.fake.planner());
    int reached = -1;
    bool closed = true;
    bool visibleRightAfter = false;
    d.setExecutor(actingAtTheThird(
        [&] {
            closed = d.close(); // what the window manager's close button does
            visibleRightAfter = d.isVisible();
        },
        &reached));
    QSignalSpy finished(&d, &QDialog::finished);
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));
    QVERIFY(d.run());
    QVERIFY2(!closed, "the window let itself be closed in the middle of a run");
    QVERIFY(visibleRightAfter);
    QCOMPARE(reached, 1);
    QVERIFY(d.isVisible());
    QCOMPARE(finished.size(), 0);
    QCOMPARE(need<QLabel>(d, u"summaryLabel")->text(), s(u"2 done, 0 skipped, 0 failed (stopped)"));
    // Afterwards it closes.
    QVERIFY(d.close());
    QVERIFY(!d.isVisible());
}

void OrganiseDialogReviewTest::notReached()
{
    // OD-RUN 4: rows the run never came to.
    Fixture f;
    f.settings.setValue(s(u"organise/target"), s(u"/out"));
    OrganiseDialog d(kFiles, &f.settings);
    d.setPlanner(f.fake.planner());
    QPushButton *close = need<QPushButton>(d, u"closeButton");
    int reached = -1;
    d.setExecutor(actingAtTheThird([&] { QTest::mouseClick(close, Qt::LeftButton); }, &reached));
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));
    const QStringList before = column(need<QTreeWidget>(d, u"previewTable"), 2);
    QCOMPARE(before.size(), 5);
    QVERIFY(!before.contains(s(u"Not reached")));
    QVERIFY(d.run());
    QCOMPARE(column(need<QTreeWidget>(d, u"previewTable"), 2),
        QStringList({ s(u"Done"), s(u"Done"), s(u"Not reached"), s(u"Not reached"), s(u"Not reached") }));
    // A run that reaches everything leaves no such row.
    Fixture g;
    g.settings.setValue(s(u"organise/target"), s(u"/out"));
    OrganiseDialog whole(kFiles, &g.settings);
    g.fake.failIndex = 0; // so that it stays open
    g.attach(whole);
    whole.show();
    QVERIFY(QTest::qWaitForWindowExposed(&whole));
    QVERIFY(whole.run());
    QVERIFY(whole.isVisible());
    const QStringList after = column(need<QTreeWidget>(whole, u"previewTable"), 2);
    QCOMPARE(after.size(), 5);
    QVERIFY(!after.contains(s(u"Not reached")));
}

QTEST_MAIN(OrganiseDialogReviewTest)
#include "tst_ui_organise_review.moc"
