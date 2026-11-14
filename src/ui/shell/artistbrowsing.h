#pragma once
#include <QObject>

class QSettings;

// Puts the artist browser beside the track list and makes its choice narrow
// the list. Contract: docs/spec/artist-browser.md (AB-SHELL). It owns no
// rule: the browser says what was chosen, the library shell does the rest.
namespace linuxbee::ui {

class ArtistBrowser;
class LibraryShell;

class ArtistBrowsing : public QObject {
    Q_OBJECT
public:
    // Neither is owned; both must outlive this object.
    ArtistBrowsing(LibraryShell *shell, QSettings *settings, QObject *parent = nullptr);
    ~ArtistBrowsing() override;

    ArtistBrowser *browser() const; // null while it is not shown

    bool isShown() const;
    void setShown(bool shown); // AB-SHELL; stored at once

private:
    struct Private;
    Private *d;
};

}
