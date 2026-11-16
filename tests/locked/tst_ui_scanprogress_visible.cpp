// LOCKED ACCEPTANCE TEST (the scan's row can be seen: beside a message, and uncut). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/scan-runner.md (SP-ROW, "Seen").
// Runs on the offscreen platform with the window shown. "Seen" here is
// QWidget::isVisible(), which is false for a widget the status bar has
// put away, though isHidden() stays false for a widget inside it.
#include <QAction>
#include <QLabel>
#include <QProgressBar>
#include <QSemaphore>
#include <QSettings>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

#include "actions/actionregistry.h"
#include "library/library.h"
#include "mainwindow/mainwindow.h"
#include "shell/scanprogress.h"
#include "shell/scanrunner.h"

using namespace linuxbee;
using namespace linuxbee::ui;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

const QString kQuestion = QStringLiteral("Stop the scan? The files stored so far stay in the library.");

struct Rig {
    QTemporaryDir dir;
    std::unique_ptr<Library> library;
    QSettings settings;
    ActionRegistry actions;
    MainWindow window;
    std::unique_ptr<ScanProgress> progress;
    bool yes = true;
    QStringList asked;
    QDateTime now = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);

    Rig()
        : library(Library::open(QStringLiteral(":memory:")))
        , settings(dir.path() + QStringLiteral("/s.ini"), QSettings::IniFormat)
        , window(&actions, &settings)
    {
        ScanProgress::Hooks hooks;
        hooks.confirm = [this](const QString &question) {
            asked.append(question);
            return yes;
        };
        progress = std::make_unique<ScanProgress>(&window, &actions, hooks);
    }
    QWidget *row() const { return window.statusBar()->findChild<QWidget *>(s(u"scanProgressRow")); }
    QProgressBar *bar() const { return window.statusBar()->findChild<QProgressBar *>(s(u"scanProgress")); }
    QLabel *text() const { return window.statusBar()->findChild<QLabel *>(s(u"scanProgressText")); }
    QToolButton *button() const { return window.statusBar()->findChild<QToolButton *>(s(u"scanStopButton")); }
    QAction *command() const { return actions.action(s(u"file.scanStop")); }
    bool rowHidden() const { return row() && row()->isHidden(); }
    // Three files to scan, in a folder of their own; returns the folder.
    QString music()
    {
        const QString folder = dir.path() + QStringLiteral("/music");
        QDir().mkpath(folder);
        int n = 0;
        for (const char *name : { "basic.flac", "basic.m4a", "basic.ogg" }) {
            if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/") + QLatin1String(name),
                    folder + QStringLiteral("/%1-").arg(++n) + QLatin1String(name)))
                QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
        }
        return folder;
    }
    ScanOptions options() const
    {
        ScanOptions o;
        o.category = QStringLiteral("Music");
        o.now = now;
        return o;
    }
};

// A runner's hook that holds the looking until the test lets it go on.
struct Gate {
    QSemaphore go;
    ScanRunnerHooks hooks()
    {
        ScanRunnerHooks h;
        h.looking = [this](int, int) {
            if (!go.tryAcquire(1, 10000))
                qWarning("the test never let this scan go on");
        };
        return h;
    }
};

void settle()
{
    for (int i = 0; i < 3; ++i)
        QCoreApplication::processEvents();
}

#define NEED_ROW(r) \
    do { \
        if (!(r).row() || !(r).bar() || !(r).text() || !(r).button() || !(r).command()) \
            QFAIL("the scan's row has not been built"); \
    } while (false)

const QString kWidest = QStringLiteral("000,000 of 000,000 files");

// True when the widget is shown, has a size, and all of it lies inside the status bar.
bool seen(const Rig &r, const QWidget *w)
{
    if (!w || !w->isVisible() || w->width() <= 0 || w->height() <= 0)
        return false;
    const QRect in(w->mapTo(r.window.statusBar(), QPoint(0, 0)), w->size());
    return r.window.statusBar()->rect().contains(in);
}

}

class ScanProgressVisibleTest : public QObject {
    Q_OBJECT
private slots:
    void besideAMessage();
    void neverCut();
    void aMessageIsStillRead();
};

void ScanProgressVisibleTest::besideAMessage()
{
    // SP-ROW, Seen: a message in the status bar, whoever wrote it and however long it stays, does not put the row away.
    (void)&kQuestion;
    Rig r;
    NEED_ROW(r);
    r.window.resize(900, 500);
    r.window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&r.window));
    ScanRunner runner(r.library.get());
    r.progress->watch(&runner);

    // The message is there first, as after an earlier scan.
    r.window.statusBar()->showMessage(s(u"Scan: 1,500 added, 0 updated, 0 unchanged, 0 skipped, 0 failed"));
    emit runner.started({ s(u"/music") });
    settle();
    QVERIFY(seen(r, r.row()));
    QVERIFY(seen(r, r.bar()));
    QVERIFY(seen(r, r.text()));
    QVERIFY(seen(r, r.button()));
    QCOMPARE(r.window.statusBar()->currentMessage(), s(u"Scan: 1,500 added, 0 updated, 0 unchanged, 0 skipped, 0 failed"));

    // A message for every file, as the library shell writes them (SR-SHELL).
    for (int i = 1; i <= 40; ++i) {
        r.window.statusBar()->showMessage(QStringLiteral("Scanning: %1 of 1500 files").arg(i * 30));
        emit runner.progress(i * 30, 1500);
        if (i % 10 == 0) {
            settle();
            QVERIFY2(seen(r, r.row()), qPrintable(QString::number(i)));
        }
    }
    settle();
    QVERIFY(seen(r, r.row()));
    QCOMPARE(r.text()->text(), s(u"1,200 of 1,500 files"));
    QVERIFY(r.bar()->width() >= 60);
    QVERIFY(r.button()->isEnabled());

    // The row is at the right end; the message keeps the left.
    const int middle = r.row()->mapTo(r.window.statusBar(), r.row()->rect().center()).x();
    QVERIFY2(middle > r.window.statusBar()->width() / 2, qPrintable(QString::number(middle)));

    // Over: the row goes, with the message still up, and comes back for the next scan.
    emit runner.idle();
    settle();
    QVERIFY(!r.row()->isVisible());
    emit runner.started({ s(u"/more") });
    settle();
    QVERIFY(seen(r, r.row()));
    r.window.statusBar()->clearMessage();
    settle();
    QVERIFY(seen(r, r.row()));
    emit runner.idle();
}

void ScanProgressVisibleTest::neverCut()
{
    // SP-ROW, Seen: the text has room for six digits either side, in a narrow window too.
    Rig r;
    NEED_ROW(r);
    r.window.resize(640, 400);
    r.window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&r.window));
    ScanRunner runner(r.library.get());
    r.progress->watch(&runner);
    r.window.statusBar()->showMessage(QString(300, QLatin1Char('m'))); // far wider than the window
    emit runner.started({ s(u"/music") });
    settle();
    const int widest = r.text()->fontMetrics().horizontalAdvance(kWidest);
    QVERIFY2(r.text()->minimumWidth() >= widest, qPrintable(QStringLiteral("%1 < %2").arg(r.text()->minimumWidth()).arg(widest)));
    for (const QPair<int, int> &at : { qMakePair(0, 0), qMakePair(397, 1500), qMakePair(1500, 17000), qMakePair(123456, 654321) }) {
        emit runner.progress(at.first, at.second);
        settle();
        QVERIFY(seen(r, r.row()));
        QVERIFY(seen(r, r.text()));
        const int needs = r.text()->fontMetrics().horizontalAdvance(r.text()->text());
        QVERIFY2(r.text()->contentsRect().width() >= needs,
            qPrintable(QStringLiteral("\"%1\" needs %2, has %3").arg(r.text()->text()).arg(needs).arg(r.text()->contentsRect().width())));
        QVERIFY(seen(r, r.button()));
    }
    QCOMPARE(r.text()->text(), s(u"123,456 of 654,321 files"));
    // The width does not change with the numbers: the row does not twitch while it counts.
    const int width = r.row()->width();
    emit runner.progress(7, 9);
    settle();
    QCOMPARE(r.row()->width(), width);
    emit runner.idle();
}

void ScanProgressVisibleTest::aMessageIsStillRead()
{
    // SP-ROW, Seen: with no scan running the row takes no room from a message.
    Rig r;
    NEED_ROW(r);
    r.window.resize(900, 500);
    r.window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&r.window));
    settle();
    QVERIFY(!r.row()->isVisible());
    const QList<QWidget *> inBar = r.window.statusBar()->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
    int taken = 0;
    for (const QWidget *w : inBar) {
        if (w->isVisible() && w->isAncestorOf(r.row()))
            taken += w->width();
    }
    QVERIFY2(taken <= 8, qPrintable(QString::number(taken)));
}

QTEST_MAIN(ScanProgressVisibleTest)
#include "tst_ui_scanprogress_visible.moc"
