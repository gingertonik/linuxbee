#include "shell/scanprogress.h"

#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QProgressBar>
#include <QStatusBar>
#include <QToolButton>
#include <QWidget>

#include <algorithm>
#include <vector>

#include "actions/actionregistry.h"
#include "mainwindow/mainwindow.h"
#include "shell/scanrunner.h"

// Contract: docs/spec/scan-runner.md (SP-x).
namespace linuxbee::ui {

namespace {

// SP-VALUE: a count with a comma between thousands.
QString counted(int n)
{
    return QLocale(QLocale::English, QLocale::UnitedStates).toString(n);
}

}

struct ScanProgress::Private {
    // One watched runner (SP-WATCH).
    struct Entry {
        QPointer<ScanRunner> runner; // a runner that is destroyed drops out at once
        bool scanning = false; // from `started` until `idle`
        qint64 order = 0; // when it began scanning; the lowest is shown
        bool hasProgress = false; // `progress` has been heard since `started`
        int done = 0;
        int total = 0;
    };

    // What the widgets show (SP-VALUE, SP-STOP); the widgets are written only when this changes.
    struct Shown {
        bool visible = false;
        int maximum = 0;
        int value = 0;
        QString text;
        bool stoppable = false;
        bool operator==(const Shown &) const = default;
    };

    ScanProgress *q = nullptr;
    QPointer<MainWindow> window;
    ActionRegistry *actions = nullptr;
    Hooks hooks;
    std::vector<Entry> entries;
    qint64 counter = 0;
    bool stopping = false;
    bool scheduled = false;
    int updates = 0;
    Shown written; // what the widgets show now
    QWidget *holder = nullptr; // the status bar's item; it is never hidden by this object
    QWidget *row = nullptr;
    QProgressBar *bar = nullptr;
    QLabel *text = nullptr;
    QToolButton *button = nullptr;

    Entry *find(QObject *runner)
    {
        for (Entry &e : entries) {
            if (e.runner == runner)
                return &e;
        }
        return nullptr;
    }

    // The runner the row shows: of those scanning, the one that began first.
    const Entry *shownEntry() const
    {
        const Entry *best = nullptr;
        for (const Entry &e : entries) {
            if (e.runner && e.scanning && (!best || e.order < best->order))
                best = &e;
        }
        return best;
    }

    void begin(Entry &e)
    {
        if (!e.scanning)
            e.order = ++counter;
        e.scanning = true;
        e.hasProgress = false;
        e.done = 0;
        e.total = 0;
    }

    // Forgets runners that are gone, and ends a stop that has nothing left to wait for.
    void tidy()
    {
        std::erase_if(entries, [](const Entry &e) { return e.runner.isNull(); });
        if (!shownEntry())
            stopping = false;
    }

    // SP-ONCE: the widgets are written at the next turn of the event loop, once.
    void schedule()
    {
        if (scheduled)
            return;
        scheduled = true;
        QMetaObject::invokeMethod(
            q,
            [this] {
                scheduled = false;
                write();
            },
            Qt::QueuedConnection);
    }

    void changed()
    {
        tidy();
        schedule();
    }

    Shown wanted() const
    {
        Shown s;
        const Entry *e = shownEntry();
        if (!e)
            return s;
        s.visible = true;
        s.stoppable = !stopping;
        if (!e->hasProgress) {
            s.maximum = 0;
            s.value = 0;
            s.text = ScanProgress::tr("Looking for files\u2026");
        } else if (e->total <= 0) {
            s.maximum = 1;
            s.value = 1;
            s.text = ScanProgress::tr("0 of 0 files");
        } else {
            s.maximum = e->total;
            s.value = std::clamp(e->done, 0, e->total);
            s.text = (e->total == 1 ? ScanProgress::tr("%1 of %2 file")
                                    : ScanProgress::tr("%1 of %2 files"))
                         .arg(counted(e->done), counted(e->total));
        }
        if (stopping)
            s.text = ScanProgress::tr("Stopping\u2026");
        return s;
    }

    // The one place that writes the widgets.
    void write()
    {
        const Shown s = wanted();
        if (s == written)
            return;
        written = s;
        ++updates;
        bar->setRange(0, s.maximum);
        bar->setValue(s.value);
        text->setText(s.text);
        button->setEnabled(s.stoppable);
        if (QAction *a = actions->action(QStringLiteral("file.scanStop")))
            a->setEnabled(s.visible && s.stoppable);
        row->setVisible(s.visible);
    }

    bool confirm(const QString &question) const
    {
        if (hooks.confirm)
            return hooks.confirm(question);
        return QMessageBox::question(window, ScanProgress::tr("Stop the scan"), question)
            == QMessageBox::Yes;
    }
};

ScanProgress::ScanProgress(
    MainWindow *window, ActionRegistry *actions, Hooks hooks, QObject *parent)
    : QObject(parent)
    , d(new Private)
{
    d->q = this;
    d->window = window;
    d->actions = actions;
    d->hooks = std::move(hooks);

    d->holder = new QWidget;
    auto *layout = new QHBoxLayout(d->holder);
    layout->setContentsMargins(0, 0, 0, 0);
    d->row = new QWidget(d->holder);
    d->row->setObjectName(QStringLiteral("scanProgressRow"));
    auto *rowLayout = new QHBoxLayout(d->row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    d->bar = new QProgressBar(d->row);
    d->bar->setObjectName(QStringLiteral("scanProgress"));
    d->bar->setTextVisible(false);
    d->bar->setRange(0, 0);
    d->text = new QLabel(d->row);
    d->text->setObjectName(QStringLiteral("scanProgressText"));
    // Room for six digits either side, so the text is never cut and the row keeps one width.
    {
        const QMargins m = d->text->contentsMargins();
        d->text->setMinimumWidth(
            d->text->fontMetrics().horizontalAdvance(QStringLiteral("000,000 of 000,000 files"))
            + m.left() + m.right() + 2 * d->text->frameWidth() + 2 * d->text->margin() + 4);
    }
    d->button = new QToolButton(d->row);
    d->button->setObjectName(QStringLiteral("scanStopButton"));
    d->button->setText(QStringLiteral("\u2715"));
    d->button->setToolTip(tr("Stop the scan"));
    rowLayout->addWidget(d->bar);
    rowLayout->addWidget(d->text);
    rowLayout->addWidget(d->button);
    layout->addWidget(d->row);
    window->statusBar()->addPermanentWidget(d->holder); // never put away by a message
    d->row->hide();

    connect(d->button, &QToolButton::clicked, this, [this] { stop(); });
    actions->implement(QStringLiteral("file.scanStop"), this, [this] { stop(); });
    if (QAction *a = actions->action(QStringLiteral("file.scanStop")))
        a->setEnabled(false);
}

ScanProgress::~ScanProgress()
{
    if (d->window)
        d->window->statusBar()->removeWidget(d->holder);
    delete d->holder;
    delete d;
}

void ScanProgress::watch(ScanRunner *runner)
{
    if (!runner || d->find(runner))
        return;
    Private::Entry entry;
    entry.runner = runner;
    d->entries.push_back(entry);
    if (runner->isRunning())
        d->begin(d->entries.back());

    connect(runner, &ScanRunner::started, this, [this, runner] {
        if (Private::Entry *e = d->find(runner)) {
            d->begin(*e);
            d->changed();
        }
    });
    connect(runner, &ScanRunner::progress, this, [this, runner](int done, int total) {
        if (Private::Entry *e = d->find(runner)) {
            e->hasProgress = true;
            e->done = done;
            e->total = total;
            d->changed();
        }
    });
    connect(runner, &ScanRunner::idle, this, [this, runner] {
        if (Private::Entry *e = d->find(runner)) {
            e->scanning = false;
            d->changed();
        }
    });
    connect(runner, &QObject::destroyed, this, [this] { d->changed(); });
    d->changed();
}

bool ScanProgress::isShown() const
{
    return d->shownEntry() != nullptr;
}

bool ScanProgress::stop()
{
    if (!isShown() || d->stopping)
        return false;
    if (!d->confirm(tr("Stop the scan? The files stored so far stay in the library.")))
        return false;
    // The question may have taken a while: the scans may have ended meanwhile.
    d->tidy();
    if (!isShown())
        return false;
    std::vector<QPointer<ScanRunner>> toStop;
    for (const Private::Entry &e : d->entries) {
        if (e.runner && e.scanning)
            toStop.push_back(e.runner);
    }
    d->stopping = true;
    for (const QPointer<ScanRunner> &runner : toStop) {
        if (runner)
            runner->cancel();
    }
    d->changed();
    return true;
}

int ScanProgress::updates() const
{
    return d->updates;
}

}
