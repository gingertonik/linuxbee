#include "shell/artistbrowsing.h"

// Stub. Plan 11c replaces every body in this file.
namespace linuxbee::ui {

struct ArtistBrowsing::Private { };

ArtistBrowsing::ArtistBrowsing(LibraryShell *shell, QSettings *settings, QObject *parent)
    : QObject(parent)
    , d(nullptr)
{
    Q_UNUSED(shell);
    Q_UNUSED(settings);
}

ArtistBrowsing::~ArtistBrowsing()
{
    Q_UNUSED(d);
}

ArtistBrowser *ArtistBrowsing::browser() const
{
    return nullptr;
}

bool ArtistBrowsing::isShown() const
{
    return false;
}

void ArtistBrowsing::setShown(bool shown)
{
    Q_UNUSED(shown);
}

}
