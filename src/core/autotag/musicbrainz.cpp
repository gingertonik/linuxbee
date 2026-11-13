#include "musicbrainz.h"

// Stub. Plan 18 replaces every body in this file.
namespace linuxbee {

// M-MATCH: an answer that is not ok matches no file (releaseTrack -1, score 0).
// The rest of matching is task 4.
QList<TrackMatch> matchTracks(const QList<LocalTrack> &local, const Release &release)
{
    QList<TrackMatch> result;
    if (!release.ok)
        result.resize(local.size());
    return result;
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
