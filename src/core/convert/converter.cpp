#include "converter.h"

// Stub. Plan 08 replaces every body in this file.
namespace linuxbee {

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
