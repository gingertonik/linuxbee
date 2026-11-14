#include "shell/albumviewing.h"

// Stub. Plan 35 replaces every body in this file.
namespace linuxbee::ui {

struct AlbumViewing::Private { };

AlbumViewing::AlbumViewing(LibraryShell *shell, QSettings *settings, QObject *parent)
    : QObject(parent)
    , d(nullptr)
{
    Q_UNUSED(shell);
    Q_UNUSED(settings);
}

AlbumViewing::~AlbumViewing()
{
    Q_UNUSED(d);
}

AlbumTracksView *AlbumViewing::view() const
{
    return nullptr;
}

QString AlbumViewing::mode() const
{
    return { };
}

void AlbumViewing::setMode(const QString &mode)
{
    Q_UNUSED(mode);
}

}
