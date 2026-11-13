#pragma once
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <functional>

#include "desktop/mprisservice.h"

// What the MPRIS service keeps, shared with the adaptors in
// mprisadaptors.cpp, and the helpers both files need.
// Contract: docs/spec/desktop-mpris.md (MP-x).
// Not part of the public API: nothing here is visible outside src/ui/desktop.
namespace linuxbee::ui {

struct DesktopState {
    // Neither pointer is owned; both outlive the service (the header's contract).
    Player *player = nullptr;
    PlayQueue *queue = nullptr;
    MprisService::Hooks hooks;

    QDBusConnection bus { QString() }; // MP-START: the connection it was started on
    bool running = false; // MP-START: registered and owning the name
};

// The root interface: who linuxBee is, and what the desktop may ask of it.
// MP-ROOT.
class RootAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry)
    Q_PROPERTY(bool CanQuit READ canQuit)
    Q_PROPERTY(bool CanRaise READ canRaise)
    Q_PROPERTY(bool HasTrackList READ hasTrackList)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes)
    Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes)
public:
    RootAdaptor(MprisService *service, DesktopState &state);

    QString identity() const;
    QString desktopEntry() const;
    bool canQuit() const;
    bool canRaise() const;
    bool hasTrackList() const;
    QStringList supportedUriSchemes() const;
    QStringList supportedMimeTypes() const;

public slots:
    void Raise();
    void Quit();

private:
    DesktopState &m_state;
};

}
