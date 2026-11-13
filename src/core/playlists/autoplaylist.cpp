#include "autoplaylist.h"

// Stub. Plan 15b replaces every body in this file.
namespace linuxbee {

QList<qint64> evaluateAutoPlaylist(const AutoPlaylist &playlist, const QList<LibraryTrack> &tracks,
    const RuleContext &context, quint32 seed)
{
    Q_UNUSED(playlist);
    Q_UNUSED(tracks);
    Q_UNUSED(context);
    Q_UNUSED(seed);
    return { };
}

}
