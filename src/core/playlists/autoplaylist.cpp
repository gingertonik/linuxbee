#include "autoplaylist.h"

// Stub. Plan 15b replaces every body in this file.
namespace linuxbee {

QString limitUnitId(AutoPlaylist::LimitUnit unit)
{
    Q_UNUSED(unit);
    return { };
}

QString selectedById(AutoPlaylist::SelectedBy selectedBy)
{
    Q_UNUSED(selectedBy);
    return { };
}

QString autoPlaylistToJson(const AutoPlaylist &playlist)
{
    Q_UNUSED(playlist);
    return { };
}

bool autoPlaylistFromJson(const QString &json, AutoPlaylist *playlist, QString *error)
{
    Q_UNUSED(json);
    Q_UNUSED(playlist);
    Q_UNUSED(error);
    return false;
}

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
