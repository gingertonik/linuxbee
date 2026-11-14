#pragma once
#include <QObject>
#include <QString>

class QSettings;

// Puts the "Album and Tracks" view into the library page, beside the plain
// track list, and lets the user choose between the two. Contract:
// docs/spec/album-view.md (AV-SHELL). It owns no rule: the view says what
// was clicked, the library shell does the rest.
namespace linuxbee::ui {

class AlbumTracksView;
class LibraryShell;

class AlbumViewing : public QObject {
    Q_OBJECT
public:
    // Neither is owned; both must outlive this object.
    AlbumViewing(LibraryShell *shell, QSettings *settings, QObject *parent = nullptr);
    ~AlbumViewing() override;

    AlbumTracksView *view() const;

    QString mode() const; // "albums" or "tracks"
    void setMode(const QString &mode); // anything but "tracks" means "albums"; stored at once

signals:
    void modeChanged();

private:
    struct Private;
    Private *d;
};

}
