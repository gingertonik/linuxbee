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

// The Player interface: the requests a desktop media control makes, and what
// it is told about the state. MP-METHODS and MP-PROPS.
// Every property of the MP-PROPS table is declared here from the start,
// because a panel asks for the whole list at once.
class PlayerAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QString LoopStatus READ loopStatus WRITE setLoopStatus)
    Q_PROPERTY(double Rate READ rate WRITE setRate)
    Q_PROPERTY(bool Shuffle READ shuffle WRITE setShuffle)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(double MinimumRate READ minimumRate)
    Q_PROPERTY(double MaximumRate READ maximumRate)
    Q_PROPERTY(bool CanGoNext READ canGoNext)
    Q_PROPERTY(bool CanGoPrevious READ canGoPrevious)
    Q_PROPERTY(bool CanPlay READ canPlay)
    Q_PROPERTY(bool CanPause READ canPause)
    Q_PROPERTY(bool CanSeek READ canSeek)
    Q_PROPERTY(bool CanControl READ canControl)
public:
    PlayerAdaptor(MprisService *service, DesktopState &state);

    QString playbackStatus() const;
    QString loopStatus() const;
    double rate() const;
    bool shuffle() const;
    QVariantMap metadata() const;
    double volume() const;
    qlonglong position() const;
    double minimumRate() const;
    double maximumRate() const;
    bool canGoNext() const;
    bool canGoPrevious() const;
    bool canPlay() const;
    bool canPause() const;
    bool canSeek() const;
    bool canControl() const;

public slots:
    // MP-METHODS: the player's own functions, under the names the desktop uses.
    void Play();
    void Pause();
    void PlayPause();
    void Stop();
    void Next();
    void Previous();
    void OpenUri(const QString &uri);

    // MP-PROPS: the properties a panel may write.
    void setLoopStatus(const QString &text);
    void setShuffle(bool on);
    void setVolume(double volume);
    void setRate(double rate);

private:
    DesktopState &m_state;
};

}
