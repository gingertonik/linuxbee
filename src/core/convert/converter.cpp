#include "converter.h"

// Stub. Plan 08 replaces every body in this file.
namespace linuxbee {

QList<EncoderPreset> encoderPresets()
{
    return { };
}

std::optional<EncoderPreset> encoderPreset(const QString &id)
{
    Q_UNUSED(id);
    return std::nullopt;
}

QString formatId(AudioFormat format)
{
    Q_UNUSED(format);
    return { };
}

std::optional<AudioFormat> formatFromId(const QString &id)
{
    Q_UNUSED(id);
    return std::nullopt;
}

QString fileExtension(AudioFormat format)
{
    Q_UNUSED(format);
    return { };
}

QStringList validateSettings(const EncoderSettings &settings)
{
    Q_UNUSED(settings);
    return { };
}

QStringList ffmpegArguments(const QString &inputPath, const QString &outputPath,
    const EncoderSettings &settings, int sourceBitsPerSample)
{
    Q_UNUSED(inputPath);
    Q_UNUSED(outputPath);
    Q_UNUSED(settings);
    Q_UNUSED(sourceBitsPerSample);
    return { };
}

ConvertResult convertFile(const ConvertJob &job, const ConvertProgress &progress)
{
    Q_UNUSED(job);
    Q_UNUSED(progress);
    return { };
}

}
