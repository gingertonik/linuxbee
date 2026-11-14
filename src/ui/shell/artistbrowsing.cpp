#include "shell/artistbrowsing.h"

#include <QPointer>
#include <QSettings>

#include "browser/artistbrowser.h"
#include "shell/libraryshell.h"

// Contract: docs/spec/artist-browser.md (AB-SHELL). The shell owns the browser
// once it is set; this object holds it through a QPointer and owns no rule.
namespace linuxbee::ui {

struct ArtistBrowsing::Private {
    ArtistBrowsing *owner = nullptr;
    LibraryShell *shell = nullptr;
    QSettings *settings = nullptr;
    QPointer<ArtistBrowser> browser; // null while the browser is not shown

    // AB-SHELL: makes a browser, gives it the shell's tracks and the stored tag,
    // and puts it beside the track list.
    void show();
    // AB-SHELL: ends the narrowing and takes the browser out. Stores nothing.
    void hide();
    // AB-SHELL: a choice narrows the shell to the tracks it keeps; no choice ends the narrowing.
    void narrow();
};

void ArtistBrowsing::Private::show()
{
    if (!browser.isNull())
        return;
    auto *made = new ArtistBrowser;
    made->setField(settings->value(QStringLiteral("browser/field")).toString());
    made->setTracks(shell->nodeTracks());
    shell->setBrowser(made);
    browser = made;
    QObject::connect(made, &ArtistBrowser::choiceChanged, owner, [this] { narrow(); });
    QObject::connect(made, &ArtistBrowser::fieldChanged, owner, [this] {
        if (!browser.isNull())
            settings->setValue(QStringLiteral("browser/field"), browser->field());
    });
}

void ArtistBrowsing::Private::hide()
{
    if (browser.isNull())
        return;
    shell->setNarrowing({ });
    shell->setBrowser(nullptr); // deletes the browser; QPointer becomes null
}

void ArtistBrowsing::Private::narrow()
{
    if (!browser.isNull() && browser->hasChoice()) {
        const QPointer<ArtistBrowser> chooser = browser;
        shell->setNarrowing([chooser](const LibraryTrack &track) {
            return chooser.isNull() || chooser->keeps(track);
        });
    } else {
        shell->setNarrowing({ });
    }
}

ArtistBrowsing::ArtistBrowsing(LibraryShell *shell, QSettings *settings, QObject *parent)
    : QObject(parent)
    , d(new Private)
{
    d->owner = this;
    d->shell = shell;
    d->settings = settings;
    // The browser follows the shell's tracks, now and at every change of them.
    connect(shell, &LibraryShell::nodeTracksChanged, this, [this] {
        if (!d->browser.isNull())
            d->browser->setTracks(d->shell->nodeTracks());
    });
    // Shown unless the stored text is false (any case) or 0. Read as text.
    const QString stored = settings->value(QStringLiteral("browser/artistsShown")).toString();
    const bool hidden = stored.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0
        || stored == QStringLiteral("0");
    if (!hidden)
        d->show();
}

ArtistBrowsing::~ArtistBrowsing()
{
    d->hide();
    delete d;
}

ArtistBrowser *ArtistBrowsing::browser() const
{
    return d->browser.data();
}

bool ArtistBrowsing::isShown() const
{
    return !d->browser.isNull();
}

void ArtistBrowsing::setShown(bool shown)
{
    if (shown)
        d->show();
    else
        d->hide();
    d->settings->setValue(QStringLiteral("browser/artistsShown"),
        shown ? QStringLiteral("true") : QStringLiteral("false"));
}

}
