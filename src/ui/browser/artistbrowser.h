#pragma once
#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

#include "library/library.h"

// A list of the artists of the tracks on show, to narrow the track list to
// one of them. Contract: docs/spec/artist-browser.md (AB-LIST, AB-CHOICE,
// AB-FILTER, AB-BYFIELD). "Artist" in the names below means a value of the
// tag being browsed by, which is Album Artist unless another is chosen. It is given tracks and says
// what was chosen; it reads no library itself.
namespace linuxbee::ui {

class ArtistBrowser : public QWidget {
    Q_OBJECT
public:
    explicit ArtistBrowser(QWidget *parent = nullptr);
    ~ArtistBrowser() override;

    void setTracks(const QList<LibraryTrack> &tracks); // AB-LIST

    // AB-BYFIELD: the tag browsed by. A name browseFields() does not hold
    // means Album Artist.
    QString field() const;
    void setField(const QString &field);

    QStringList artists() const; // every artist held, in list order; "" is "no artist"
    bool hasChoice() const; // false: all artists
    QString currentArtist() const; // meaningful only with a choice
    void chooseArtist(const QString &name); // an artist not held: all
    void chooseAll();

    // AB-CHOICE: whether a track belongs to what is chosen. True for every
    // track while there is no choice.
    bool keeps(const LibraryTrack &track) const;

signals:
    void choiceChanged();
    void fieldChanged();

private:
    struct Private;
    Private *d;
};

}
