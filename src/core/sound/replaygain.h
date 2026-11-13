#pragma once
#include <QString>
#include <QStringList>

#include <optional>

#include "tags/tagreader.h"
#include "tags/tagwriter.h"

// Volume levelling: reading gain tags, turning them into a playback volume,
// measuring loudness and writing the tags. Contract: docs/spec/replay-gain.md.
// No audio data is ever changed.
namespace linuxbee {

struct GainValues {
    std::optional<double> trackGainDb;
    std::optional<double> trackPeak; // linear; 1.0 is full scale
    std::optional<double> albumGainDb;
    std::optional<double> albumPeak;
};

// N-TAGS
GainValues gainFromTags(const TrackTags &tags);

struct GainOptions {
    enum class Mode { Off, Track, Album };
    Mode mode = Mode::Track;
    double preampDb = 0.0; // added to every track that has a gain
    double untaggedDb = 0.0; // used for a track that has none
    bool preventClipping = true;
};

// N-APPLY: the factor to multiply the volume by. 1.0 changes nothing.
double gainFactor(const GainValues &values, const GainOptions &options = { });

struct Loudness {
    bool ok = false;
    double integratedLufs = 0.0;
    double truePeakDbfs = 0.0;
    QString error; // set when !ok

    double gainDb() const; // N-REFERENCE
    double peak() const; // linear
};

// N-PARSE: reads the summary ffmpeg's ebur128 filter prints.
Loudness parseLoudnessSummary(const QString &ffmpegOutput);

// N-MEASURE: one file, or several measured as one programme (an album).
// Runs ffmpeg and waits for it. Reads the files; writes nothing.
Loudness measureLoudness(
    const QStringList &files, const QString &ffmpegProgram = QStringLiteral("ffmpeg"));

// N-WRITE
TagChanges gainTagChanges(
    const std::optional<Loudness> &track, const std::optional<Loudness> &album);
// The changes that take every gain tag off a track ("restore original volume").
TagChanges removeGainTagChanges(const TrackTags &tags);

}
