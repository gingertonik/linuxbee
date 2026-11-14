#include "albumview/albumtracksview.h"

// Stub. Plan 35 replaces every body in this file.
namespace linuxbee::ui {

struct AlbumTracksView::Private { };

AlbumTracksView::AlbumTracksView(QWidget *parent)
    : QWidget(parent)
    , d(nullptr)
{
}

AlbumTracksView::~AlbumTracksView()
{
    Q_UNUSED(d);
}

void AlbumTracksView::setTracks(const QList<LibraryTrack> &tracksInOrder)
{
    Q_UNUSED(tracksInOrder);
}

QList<AlbumGroup> AlbumTracksView::groups() const
{
    return { };
}

QList<qint64> AlbumTracksView::selectedTrackIds() const
{
    return { };
}

void AlbumTracksView::selectTracks(const QList<qint64> &ids)
{
    Q_UNUSED(ids);
}

void AlbumTracksView::jumpTo(QChar letter)
{
    Q_UNUSED(letter);
}

int AlbumTracksView::currentGroup() const
{
    return -2;
}

}
