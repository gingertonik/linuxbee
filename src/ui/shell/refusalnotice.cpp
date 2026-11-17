#include "shell/refusalnotice.h"

#include "library/library.h"

#include <QMetaObject>
#include <QPointer>
#include <QStatusBar>

// SH-REFUSED (docs/spec/library-shell.md): what the library refused is said in the status bar,
// once for each return to the event loop, and never in a dialog.
namespace linuxbee::ui {

struct RefusalNotice::Private {
    RefusalNotice *owner = nullptr;
    QPointer<QStatusBar> bar;
    QString lastLine;
    int linesSaid = 0;

    int gathered = 0; // refusals since the last line; 0 when none are waiting
    QString what; // the library's words for the last one gathered
    bool shownSince = false; // a line was put in the status bar since the first one gathered
    bool saying = false; // the notice is putting its own line there

    void gather(const QString &words);
    void onBarChanged(const QString &text);
    void say();
};

void RefusalNotice::Private::gather(const QString &words)
{
    what = words;
    if (gathered++ > 0)
        return; // the call that says it is already queued
    shownSince = false;
    // Not during the library's call: when control next returns to the event loop.
    QMetaObject::invokeMethod(owner, [this] { say(); }, Qt::QueuedConnection);
}

void RefusalNotice::Private::onBarChanged(const QString &text)
{
    // The status bar tells a line was put there (text) or cleared (empty).
    if (gathered == 0 || saying)
        return;
    shownSince = !text.isEmpty();
}

void RefusalNotice::Private::say()
{
    const int n = gathered;
    const QString words = what;
    const bool since = shownSince;
    gathered = 0; // the count begins again
    what.clear();
    shownSince = false;
    if (!bar || n == 0)
        return;

    const QString shown = bar->currentMessage();
    const QString needle = QStringLiteral("library could not be changed");
    const bool ownLine = since && !shown.isEmpty();
    if (ownLine && shown.contains(needle, Qt::CaseInsensitive))
        return; // that command worded it itself, and better

    const QString tail = n == 1 ? RefusalNotice::tr(": %1").arg(words)
                                : RefusalNotice::tr(", %1 times: %2").arg(n).arg(words);
    const QString line = ownLine
        ? shown + RefusalNotice::tr("; the library could not be changed") + tail
        : RefusalNotice::tr("The library could not be changed") + tail;

    saying = true;
    bar->showMessage(line);
    saying = false;
    lastLine = line;
    ++linesSaid;
    emit owner->said(line);
}

RefusalNotice::RefusalNotice(linuxbee::Library *library, QStatusBar *bar, QObject *parent)
    : QObject(parent)
    , d(new Private)
{
    d->owner = this;
    d->bar = bar;
    if (library)
        connect(library, &Library::failed, this, [this](const QString &what) { d->gather(what); });
    if (bar)
        connect(bar, &QStatusBar::messageChanged, this,
            [this](const QString &text) { d->onBarChanged(text); });
}

RefusalNotice::~RefusalNotice() = default;

QString RefusalNotice::lastLine() const
{
    return d->lastLine;
}

int RefusalNotice::count() const
{
    return d->linesSaid;
}

}
