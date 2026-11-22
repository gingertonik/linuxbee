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
    QPointer<linuxbee::Library> library;
    qint64 heardAnswers = 0; // the library's failedAnswers() as last heard
    bool changed = false; // one of the refusals gathered is a change (L-WORDS: not a failed read)
    QString lastLine;
    int linesSaid = 0;

    int gathered = 0; // refusals since the last line; 0 when none are waiting
    QString what; // the library's words for the last one gathered
    bool shownSince = false; // a line was put in the status bar since the first one gathered
    bool saying = false; // the notice is putting its own line there

    void gather(const QString &words);
    bool isRead(); // a `failed` that came with a new failed answer is a read
    void onBarChanged(const QString &text);
    void say();
};

// SH-REFUSED, L-WORDS: a failed answer (L-READFAIL 3) is a read; any other refusal is a change.
bool RefusalNotice::Private::isRead()
{
    if (!library)
        return false;
    const qint64 now = library->failedAnswers();
    const bool read = now != heardAnswers;
    heardAnswers = now;
    return read;
}

void RefusalNotice::Private::gather(const QString &words)
{
    if (!isRead())
        changed = true;
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
    const bool change = changed;
    changed = false;
    gathered = 0; // the count begins again
    what.clear();
    shownSince = false;
    if (!bar || n == 0)
        return;

    const QString shown = bar->currentMessage();
    // A gather with no change says "could not be read"; a line holding the same phrase is not said
    // over.
    const QString phrase = change ? RefusalNotice::tr("The library could not be changed")
                                  : RefusalNotice::tr("The library could not be read");
    const QString needle = phrase.mid(0, 1).toLower() + phrase.mid(1);
    const bool ownLine = since && !shown.isEmpty();
    if (ownLine && shown.contains(needle, Qt::CaseInsensitive))
        return; // that command worded it itself, and better

    const QString tail = n == 1 ? RefusalNotice::tr(": %1").arg(words)
                                : RefusalNotice::tr(", %1 times: %2").arg(n).arg(words);
    const QString line = ownLine ? shown + QStringLiteral("; ") + needle + tail : phrase + tail;

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
    d->library = library;
    if (library)
        d->heardAnswers = library->failedAnswers();
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
