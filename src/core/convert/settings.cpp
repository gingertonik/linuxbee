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

// C-DEPTH: the bit depth a lossless target is written at.
int losslessDepth(const EncoderSettings &s, int sourceBits)
{
    int depth = s.bitDepth;
    if (depth == 0)
        depth = (sourceBits == 24 || sourceBits == 32) ? sourceBits : 16;
    if (s.format == AudioFormat::Alac && depth == 32)
        depth = 24; // Apple Lossless has no 32-bit mode.
    return depth;
}

// C-ARGS: a quality with no trailing zeros: 0, 6.5, -1.
QString formatQuality(double quality)
{
    QString text = QString::number(quality, 'f', 6);
    while (text.endsWith(QLatin1Char('0')))
        text.chop(1);
    if (text.endsWith(QLatin1Char('.')))
        text.chop(1);
    return text;
}

// C-ARGS depth arguments. `planar` selects the s16p and s32p forms that
// Apple Lossless and WavPack use.
void appendDepth(QStringList &args, int depth, bool planar)
{
    if (depth == 16) {
        args << QStringLiteral("-sample_fmt")
             << (planar ? QStringLiteral("s16p") : QStringLiteral("s16"));
        return;
    }
    args << QStringLiteral("-sample_fmt")
         << (planar ? QStringLiteral("s32p") : QStringLiteral("s32"));
    if (depth == 24)
        args << QStringLiteral("-bits_per_raw_sample") << QStringLiteral("24");
}

// C-ARGS: the encoder arguments for one format. The settings must be valid.
QStringList encoderArguments(const EncoderSettings &s, int sourceBits)
{
    const bool byBitrate = s.rateControl == RateControl::Bitrate;
    const QString kbps = QStringLiteral("%1k").arg(s.bitrateKbps);
    QStringList args;
    switch (s.format) {
    case AudioFormat::Mp3:
        args << QStringLiteral("-c:a") << QStringLiteral("libmp3lame");
        if (byBitrate)
            args << QStringLiteral("-b:a") << kbps;
        else
            args << QStringLiteral("-q:a") << formatQuality(s.quality);
        return args;
    case AudioFormat::OggVorbis:
        args << QStringLiteral("-c:a") << QStringLiteral("libvorbis");
        if (byBitrate)
            args << QStringLiteral("-b:a") << kbps;
        else
            args << QStringLiteral("-q:a") << formatQuality(s.quality);
        return args;
    case AudioFormat::Opus:
        return { QStringLiteral("-c:a"), QStringLiteral("libopus"), QStringLiteral("-b:a"), kbps };
    case AudioFormat::Aac:
        return { QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("-b:a"), kbps };
    case AudioFormat::Flac:
        args << QStringLiteral("-c:a") << QStringLiteral("flac");
        if (s.compressionLevel != -1)
            args << QStringLiteral("-compression_level") << QString::number(s.compressionLevel);
        appendDepth(args, losslessDepth(s, sourceBits), false);
        return args;
    case AudioFormat::Alac:
        args << QStringLiteral("-c:a") << QStringLiteral("alac");
        appendDepth(args, losslessDepth(s, sourceBits), true);
        return args;
    case AudioFormat::WavPack:
        args << QStringLiteral("-c:a") << QStringLiteral("wavpack");
        appendDepth(args, losslessDepth(s, sourceBits), true);
        return args;
    case AudioFormat::Wav: {
        const int depth = losslessDepth(s, sourceBits);
        QString codec = QStringLiteral("pcm_s32le");
        if (depth == 16)
            codec = QStringLiteral("pcm_s16le");
        else if (depth == 24)
            codec = QStringLiteral("pcm_s24le");
        return { QStringLiteral("-c:a"), codec };
    }
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

// C-ARGS: the arguments ffmpeg is run with (the program name is not included).
QStringList ffmpegArguments(const QString &inputPath, const QString &outputPath,
    const EncoderSettings &settings, int sourceBitsPerSample)
{
    QStringList args = {
        QStringLiteral("-nostdin"),
        QStringLiteral("-hide_banner"),
        QStringLiteral("-loglevel"),
        QStringLiteral("error"),
        QStringLiteral("-nostats"),
        QStringLiteral("-progress"),
        QStringLiteral("pipe:1"),
        QStringLiteral("-y"),
        QStringLiteral("-i"),
        inputPath,
        QStringLiteral("-map"),
        QStringLiteral("0:a:0"),
        QStringLiteral("-map_metadata"),
        QStringLiteral("-1"),
        QStringLiteral("-map_chapters"),
        QStringLiteral("-1"),
        QStringLiteral("-fflags"),
        QStringLiteral("+bitexact"),
    };
    args += encoderArguments(settings, sourceBitsPerSample);
    if (settings.sampleRateHz != 0)
        args << QStringLiteral("-ar") << QString::number(settings.sampleRateHz);
    if (settings.channels != 0)
        args << QStringLiteral("-ac") << QString::number(settings.channels);
    args << QStringLiteral("-f") << QString::fromLatin1(formatInfo(settings.format).muxer)
         << outputPath;
    return args;
}

}
