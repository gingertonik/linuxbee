#include "desktop/mprisservice.h"
#include "desktop/mprisservice_p.h"

// The two MPRIS interfaces, seen from the bus. They hold no state of their
// own: they read and write the player and the queue through DesktopState.
// Contract: docs/spec/desktop-mpris.md (MP-x).
namespace linuxbee::ui {

RootAdaptor::RootAdaptor(MprisService *service, DesktopState &state)
    : QDBusAbstractAdaptor(service)
    , m_state(state)
{
}

QString RootAdaptor::identity() const
{
    return QStringLiteral("linuxBee");
}

QString RootAdaptor::desktopEntry() const
{
    return QStringLiteral("io.github.gingertonik.linuxbee");
}

bool RootAdaptor::canQuit() const
{
    return true;
}

bool RootAdaptor::canRaise() const
{
    return true;
}

bool RootAdaptor::hasTrackList() const
{
    return false;
}

QStringList RootAdaptor::supportedUriSchemes() const
{
    return { QStringLiteral("file") };
}

QStringList RootAdaptor::supportedMimeTypes() const
{
    return { QStringLiteral("audio/flac"), QStringLiteral("audio/mpeg"),
        QStringLiteral("audio/mp4"), QStringLiteral("audio/ogg"), QStringLiteral("audio/opus"),
        QStringLiteral("audio/x-wav") };
}

// MP-ROOT: the service knows neither the window nor the application, so both
// go through a hook. A hook that was never given is simply not there.
void RootAdaptor::Raise()
{
    if (m_state.hooks.raise)
        m_state.hooks.raise();
}

void RootAdaptor::Quit()
{
    if (m_state.hooks.quit)
        m_state.hooks.quit();
}

}
