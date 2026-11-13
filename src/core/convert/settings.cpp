#include "format_table.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace linuxbee {

namespace {

using RateControl = EncoderSettings::RateControl;

bool inList(int value, std::initializer_list<int> allowed)
{
    return std::find(allowed.begin(), allowed.end(), value) != allowed.end();
}

bool between(int value, int low, int high)
{
    return value >= low && value <= high;
}

// C-RATE, for the rate control the settings select. Returns the problem, or
// an empty string. Lossless formats ignore the rate control.
QString rateProblem(const EncoderSettings &s)
{
    const bool byBitrate = s.rateControl == RateControl::Bitrate;
    switch (s.format) {
    case AudioFormat::Mp3:
        if (byBitrate) {
            if (!inList(s.bitrateKbps,
                    { 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320 }))
                return QStringLiteral("bitrate: %1 is not an MP3 bitrate (32 to 320, in the "
                                      "usual steps)")
                    .arg(s.bitrateKbps);
            return { };
        }
        if (!(s.quality >= 0 && s.quality <= 9 && std::trunc(s.quality) == s.quality))
            return QStringLiteral("quality: %1 is not an MP3 V level (a whole number from 0 to 9)")
                .arg(s.quality);
        return { };
    case AudioFormat::OggVorbis:
        if (byBitrate) {
            if (!between(s.bitrateKbps, 32, 512))
                return QStringLiteral("bitrate: %1 is outside the Ogg Vorbis range (32 to 512)")
                    .arg(s.bitrateKbps);
            return { };
        }
        if (!(s.quality >= -1 && s.quality <= 10))
            return QStringLiteral("quality: %1 is outside the Ogg Vorbis range (-1 to 10)")
                .arg(s.quality);
        return { };
    case AudioFormat::Opus:
        if (!byBitrate)
            return QStringLiteral("quality: Opus has no quality setting (set a bitrate)");
        if (!between(s.bitrateKbps, 6, 510))
            return QStringLiteral("bitrate: %1 is outside the Opus range (6 to 510)")
                .arg(s.bitrateKbps);
        return { };
    case AudioFormat::Aac:
        if (!byBitrate)
            return QStringLiteral("quality: AAC has no quality setting (set a bitrate)");
        if (!between(s.bitrateKbps, 32, 512))
            return QStringLiteral("bitrate: %1 is outside the AAC range (32 to 512)")
                .arg(s.bitrateKbps);
        return { };
    case AudioFormat::Flac:
    case AudioFormat::Alac:
    case AudioFormat::Wav:
    case AudioFormat::WavPack:
        return { };
    }
    return { };
}

}

// C-VALID: one line per problem, each starting with the name of the setting.
QStringList validateSettings(const EncoderSettings &settings)
{
    QStringList problems;
    const FormatInfo &info = formatInfo(settings.format);

    if (!info.lossless) {
        const QString rate = rateProblem(settings);
        if (!rate.isEmpty())
            problems.append(rate);
    }

    if (!info.lossless) {
        if (settings.bitDepth != 0)
            problems.append(QStringLiteral("bit depth: %1 is set, but %2 is lossy and has no bit "
                                           "depth (use 0)")
                    .arg(settings.bitDepth)
                    .arg(QString::fromLatin1(info.id)));
    } else if (settings.bitDepth != 0 && !inList(settings.bitDepth, { 16, 24, 32 })) {
        problems.append(QStringLiteral("bit depth: %1 is not 16, 24 or 32 (or 0 to keep the "
                                       "source's)")
                .arg(settings.bitDepth));
    } else if (settings.format == AudioFormat::Alac && settings.bitDepth == 32) {
        problems.append(QStringLiteral("bit depth: Apple Lossless has no 32-bit mode (use 16 or "
                                       "24)"));
    }

    // -1 leaves the encoder's default, so it is valid on FLAC too.
    if (settings.compressionLevel != -1) {
        if (settings.format != AudioFormat::Flac)
            problems.append(QStringLiteral("compression level: %1 is set, but only FLAC has a "
                                           "compression level (use -1)")
                    .arg(settings.compressionLevel));
        else if (!between(settings.compressionLevel, 0, 12))
            problems.append(QStringLiteral("compression level: %1 is outside the FLAC range (0 to "
                                           "12)")
                    .arg(settings.compressionLevel));
    }

    if (settings.sampleRateHz != 0) {
        const int hz = settings.sampleRateHz;
        if (!inList(hz,
                { 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000, 88200, 96000,
                    176400, 192000 }))
            problems.append(QStringLiteral("sample rate: %1 Hz is not a standard rate (8000 to "
                                           "192000)")
                    .arg(hz));
        else if (settings.format == AudioFormat::Mp3 && hz > 48000)
            problems.append(QStringLiteral("sample rate: %1 Hz is above 48000, the most MP3 can "
                                           "hold")
                    .arg(hz));
        else if (settings.format == AudioFormat::Opus
            && !inList(hz, { 8000, 12000, 16000, 24000, 48000 }))
            problems.append(QStringLiteral("sample rate: %1 Hz is not one Opus takes (8000, "
                                           "12000, 16000, 24000 or 48000)")
                    .arg(hz));
    }

    if (!inList(settings.channels, { 0, 1, 2 }))
        problems.append(
            QStringLiteral("channels: %1 is not 0 (keep), 1 or 2").arg(settings.channels));

    return problems;
}

}
