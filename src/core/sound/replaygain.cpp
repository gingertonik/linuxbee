#include "replaygain.h"

// Stub. Plan 17 replaces every body in this file.
namespace linuxbee {

double Loudness::gainDb() const
{
    return 0.0;
}

double Loudness::peak() const
{
    return 0.0;
}

Loudness parseLoudnessSummary(const QString &ffmpegOutput)
{
    Q_UNUSED(ffmpegOutput);
    return { };
}

Loudness measureLoudness(const QStringList &files, const QString &ffmpegProgram)
{
    Q_UNUSED(files);
    Q_UNUSED(ffmpegProgram);
    return { };
}

TagChanges gainTagChanges(
    const std::optional<Loudness> &track, const std::optional<Loudness> &album)
{
    Q_UNUSED(track);
    Q_UNUSED(album);
    return { };
}

TagChanges removeGainTagChanges(const TrackTags &tags)
{
    Q_UNUSED(tags);
    return { };
}

}
