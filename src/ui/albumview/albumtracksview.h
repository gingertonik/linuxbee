#pragma once
#include <QChar>
#include <QList>
#include <QWidget>

#include "library/albumgroups.h"
#include "library/library.h"

// The "Album and Tracks" view: the tracks it is given, under a heading per
// album, with a jump bar. Contract: docs/spec/album-view.md (AV-VIEW,
// AV-SELECT, AV-ACTIVATE, AV-BAR). It is given tracks and reports what the
// user did; it reads no library and plays nothing.
namespace linuxbee::ui {

class AlbumTracksView : public QWidget {
    Q_OBJECT
public:
    explicit AlbumTracksView(QWidget *parent = nullptr);
    ~AlbumTracksView() override;

    // The tracks, already in the order to show (AV-ORDER is the caller's business).
    void setTracks(const QList<LibraryTrack> &tracksInOrder);
    QList<AlbumGroup> groups() const;

    QList<qint64> selectedTrackIds() const; // AV-SELECT; in the order shown
    void selectTracks(const QList<qint64> &ids);

    void jumpTo(QChar letter); // AV-BAR
    int currentGroup() const; // the album the current row is in; -1: no current row

signals:
    void selectionChanged();
    void trackActivated(qint64 trackId); // AV-ACTIVATE
    void albumActivated(int group);
    void menuRequested(const QPoint &globalPos); // a right click on a row

private:
    struct Private;
    Private *d;
};

}
