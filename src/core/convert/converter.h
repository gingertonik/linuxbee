#pragma once
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

#include "place_io.h"
#include "tags/tagwriter.h"

// Converts one music file to another format with ffmpeg, then copies the tags
// and pictures across with the tag writer. Contract: docs/spec/convert.md.
namespace linuxbee {

enum class AudioFormat { Mp3, Flac, OggVorbis, Opus, Aac, Alac, Wav, WavPack };

struct EncoderSettings {
    AudioFormat format = AudioFormat::Mp3;

    // Lossy formats only (C-RATE).
    enum class RateControl { Bitrate, Quality };
    RateControl rateControl = RateControl::Bitrate;
    int bitrateKbps = 320;
    double quality = 0; // MP3: 0 (best) to 9. Ogg Vorbis: -1 to 10 (best).

    // FLAC only: 0 to 12. -1 leaves the encoder's default.
    int compressionLevel = -1;

    // 0 keeps what the source has.
    int sampleRateHz = 0;
    int bitDepth = 0; // lossless formats only: 16, 24 or 32
    int channels = 0; // 1 or 2
};

struct EncoderPreset {
    QString id; // "mp3-320"
    QString name; // "MP3 320 kbps (CBR)"
    EncoderSettings settings;
};

// C-PRESETS, in display order. The first is the default.
QList<EncoderPreset> encoderPresets();
std::optional<EncoderPreset> encoderPreset(const QString &id);

// C-FORMATS
QString formatId(
    AudioFormat format); // "mp3", "flac", "ogg", "opus", "aac", "alac", "wav", "wavpack"
std::optional<AudioFormat> formatFromId(const QString &id);
QString fileExtension(AudioFormat format); // without a dot: "mp3", "m4a", ...

// C-VALID: one line per problem; empty when the settings can be used.
QStringList validateSettings(const EncoderSettings &settings);

// C-ARGS: the arguments ffmpeg is run with (the program name is not included).
// `sourceBitsPerSample` is AudioInfo::bitsPerSample of the source (0 for a
// lossy source). The settings must be valid.
QStringList ffmpegArguments(const QString &inputPath, const QString &outputPath,
    const EncoderSettings &settings, int sourceBitsPerSample);

struct ConvertJob {
    QString source;
    QString destination; // the full path of the file to create
    EncoderSettings settings;

    enum class IfExists { Skip, Overwrite, Rename, UpdateTags };
    IfExists ifExists = IfExists::Skip;

    bool copyTags = true;
    bool copyPictures = true; // only when copyTags is true
    WriteOptions tagOptions;

    QString ffmpegPath = QStringLiteral("ffmpeg");

    // C-REPLACE, the count is watched: how long ffprobe may say nothing before it is ended.
    int countSilenceMs = 30000;

    // C-PLACE: the three calls of step 9. The system's, unless a test gives others.
    convert::PlaceIo placeIo;
};

struct ConvertResult {
    enum class Status {
        Converted,
        Skipped, // the destination exists (IfExists::Skip)
        TagsUpdated, // the destination exists; only its tags were brought in step
        Cancelled,
        SourceNotFound,
        SourceNotAudio,
        InvalidJob, // nothing was done; see message
        EncoderMissing, // ffmpeg could not be started
        Failed, // see message
    };
    Status status = Status::Failed;
    QString outputPath; // Converted and TagsUpdated: the file that now holds the result
    QString message;
    QStringList warnings; // tags that could not be carried over

    bool ok() const
    {
        return status == Status::Converted || status == Status::Skipped
            || status == Status::TagsUpdated;
    }
};

// Called with a fraction from 0 to 1. Returning false cancels the job.
using ConvertProgress = std::function<bool(double fraction)>;

// Blocks until the job is done. May be called from several threads at once
// for different destinations.
ConvertResult convertFile(const ConvertJob &job, const ConvertProgress &progress = { });

}
