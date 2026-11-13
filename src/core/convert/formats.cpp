#include "format_table.h"

namespace linuxbee {

namespace {

// C-FORMATS. The ffmpeg encoder is chosen in settings.cpp, because the
// encoder for Wav depends on the bit depth.
const FormatInfo kFormats[] = {
    { AudioFormat::Mp3, "mp3", "mp3", "mp3", false },
    { AudioFormat::Flac, "flac", "flac", "flac", true },
    { AudioFormat::OggVorbis, "ogg", "ogg", "ogg", false },
    { AudioFormat::Opus, "opus", "opus", "opus", false },
    { AudioFormat::Aac, "aac", "m4a", "ipod", false },
    { AudioFormat::Alac, "alac", "m4a", "ipod", true },
    { AudioFormat::Wav, "wav", "wav", "wav", true },
    { AudioFormat::WavPack, "wavpack", "wv", "wv", true },
};

// C-PRESETS helpers. A preset starts from the defaults, which keep what the
// source has.
EncoderSettings bitrateSettings(AudioFormat format, int kbps)
{
    EncoderSettings settings;
    settings.format = format;
    settings.rateControl = EncoderSettings::RateControl::Bitrate;
    settings.bitrateKbps = kbps;
    return settings;
}

EncoderSettings qualitySettings(AudioFormat format, double quality)
{
    EncoderSettings settings;
    settings.format = format;
    settings.rateControl = EncoderSettings::RateControl::Quality;
    settings.quality = quality;
    return settings;
}

EncoderSettings losslessSettings(AudioFormat format, int compressionLevel = -1)
{
    EncoderSettings settings;
    settings.format = format;
    settings.compressionLevel = compressionLevel;
    return settings;
}

EncoderPreset makePreset(const char *id, const char *name, const EncoderSettings &settings)
{
    return { QString::fromLatin1(id), QString::fromLatin1(name), settings };
}

}

const FormatInfo &formatInfo(AudioFormat format)
{
    for (const FormatInfo &row : kFormats) {
        if (row.format == format)
            return row;
    }
    return kFormats[0];
}

QString formatId(AudioFormat format)
{
    return QString::fromLatin1(formatInfo(format).id);
}

std::optional<AudioFormat> formatFromId(const QString &id)
{
    for (const FormatInfo &row : kFormats) {
        if (id.compare(QString::fromLatin1(row.id), Qt::CaseInsensitive) == 0)
            return row.format;
    }
    return std::nullopt;
}

QString fileExtension(AudioFormat format)
{
    return QString::fromLatin1(formatInfo(format).extension);
}

// C-PRESETS, in this order. The first is the default.
QList<EncoderPreset> encoderPresets()
{
    EncoderSettings flacCd = losslessSettings(AudioFormat::Flac, 5);
    flacCd.sampleRateHz = 44100;
    flacCd.bitDepth = 16;

    return {
        makePreset("mp3-320", "MP3 320 kbps (CBR)", bitrateSettings(AudioFormat::Mp3, 320)),
        makePreset("mp3-v0", "MP3 V0 (VBR, about 245 kbps)", qualitySettings(AudioFormat::Mp3, 0)),
        makePreset("mp3-v2", "MP3 V2 (VBR, about 190 kbps)", qualitySettings(AudioFormat::Mp3, 2)),
        makePreset("mp3-192", "MP3 192 kbps (CBR)", bitrateSettings(AudioFormat::Mp3, 192)),
        makePreset("mp3-128", "MP3 128 kbps (CBR)", bitrateSettings(AudioFormat::Mp3, 128)),
        makePreset("flac", "FLAC", losslessSettings(AudioFormat::Flac, 5)),
        makePreset("flac-8", "FLAC (smallest files)", losslessSettings(AudioFormat::Flac, 8)),
        makePreset("flac-cd", "FLAC, 16-bit 44.1 kHz", flacCd),
        makePreset(
            "ogg-q5", "Ogg Vorbis q5 (about 160 kbps)", qualitySettings(AudioFormat::OggVorbis, 5)),
        makePreset(
            "ogg-q8", "Ogg Vorbis q8 (about 256 kbps)", qualitySettings(AudioFormat::OggVorbis, 8)),
        makePreset("opus-128", "Opus 128 kbps", bitrateSettings(AudioFormat::Opus, 128)),
        makePreset("opus-96", "Opus 96 kbps", bitrateSettings(AudioFormat::Opus, 96)),
        makePreset("opus-192", "Opus 192 kbps", bitrateSettings(AudioFormat::Opus, 192)),
        makePreset("aac-256", "AAC 256 kbps", bitrateSettings(AudioFormat::Aac, 256)),
        makePreset("aac-160", "AAC 160 kbps", bitrateSettings(AudioFormat::Aac, 160)),
        makePreset("alac", "Apple Lossless", losslessSettings(AudioFormat::Alac)),
        makePreset("wav", "WAV", losslessSettings(AudioFormat::Wav)),
        makePreset("wavpack", "WavPack", losslessSettings(AudioFormat::WavPack)),
    };
}

std::optional<EncoderPreset> encoderPreset(const QString &id)
{
    for (const EncoderPreset &preset : encoderPresets()) {
        if (preset.id == id)
            return preset;
    }
    return std::nullopt;
}

}
