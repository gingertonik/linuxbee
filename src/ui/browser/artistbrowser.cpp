#include "browser/artistbrowser.h"

// Stub. Plan 11c replaces every body in this file.
namespace linuxbee::ui {

struct ArtistBrowser::Private { };

ArtistBrowser::ArtistBrowser(QWidget *parent)
    : QWidget(parent)
    , d(nullptr)
{
}

ArtistBrowser::~ArtistBrowser()
{
    Q_UNUSED(d);
}

void ArtistBrowser::setTracks(const QList<LibraryTrack> &tracks)
{
    Q_UNUSED(tracks);
}

QString ArtistBrowser::field() const
{
    return { };
}
void ArtistBrowser::setField(const QString &field)
{
    Q_UNUSED(field);
}
QStringList ArtistBrowser::artists() const
{
    return { };
}

bool ArtistBrowser::hasChoice() const
{
    return false;
}

QString ArtistBrowser::currentArtist() const
{
    return { };
}

void ArtistBrowser::chooseArtist(const QString &name)
{
    Q_UNUSED(name);
}

void ArtistBrowser::chooseAll()
{
}

bool ArtistBrowser::keeps(const LibraryTrack &track) const
{
    Q_UNUSED(track);
    return true;
}

}
