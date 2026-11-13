#include "musicbrainz.h"

// Stub. Plan 18 replaces every body in this file.
namespace linuxbee {

ReleaseSearch parseReleaseSearch(const QByteArray &json)
{
    Q_UNUSED(json);
    return { };
}

int Release::tracksOnDisc(int disc) const
{
    Q_UNUSED(disc);
    return 0;
}

Release parseRelease(const QByteArray &json)
{
    Q_UNUSED(json);
    return { };
}

QList<TrackMatch> matchTracks(const QList<LocalTrack> &local, const Release &release)
{
    Q_UNUSED(local);
    Q_UNUSED(release);
    return { };
}

TagChanges releaseTagChanges(
    const TrackTags &tags, const Release &release, int releaseTrack, const AutoTagOptions &options)
{
    Q_UNUSED(tags);
    Q_UNUSED(release);
    Q_UNUSED(releaseTrack);
    Q_UNUSED(options);
    return { };
}

}
