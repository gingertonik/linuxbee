#include "playback/audioengine.h"

namespace linuxbee {

// AE-FORMAT: the file's own rate when it lies from 8000 to 192000 Hz, else 44100;
// one channel for mono, two for everything else (more than two are mixed down).
PcmFormat playbackFormat(const AudioInfo &info)
{
    PcmFormat format;
    if (info.sampleRateHz >= 8000 && info.sampleRateHz <= 192000)
        format.sampleRate = info.sampleRateHz;
    else
        format.sampleRate = 44100;
    format.channels = info.channels == 1 ? 1 : 2;
    return format;
}

// AE-ARGS: -v error -nostdin [-ss SECONDS] -i PATH -map 0:a:0 -vn -sn -dn
//          -f f32le -acodec pcm_f32le -ar RATE -ac CHANNELS pipe:1
QStringList decodeArguments(const QString &path, qint64 startMs, const PcmFormat &format)
{
    QStringList args {
        QStringLiteral("-v"),
        QStringLiteral("error"),
        QStringLiteral("-nostdin"),
    };
    if (startMs > 0) {
        // Built from integers, so "83.456" comes out exactly and no float rounds it.
        const QString seconds = QStringLiteral("%1.%2")
                                    .arg(startMs / 1000)
                                    .arg(startMs % 1000, 3, 10, QLatin1Char('0'));
        args << QStringLiteral("-ss") << seconds;
    }
    args << QStringLiteral("-i") << path << QStringLiteral("-map") << QStringLiteral("0:a:0")
         << QStringLiteral("-vn") << QStringLiteral("-sn") << QStringLiteral("-dn")
         << QStringLiteral("-f") << QStringLiteral("f32le") << QStringLiteral("-acodec")
         << QStringLiteral("pcm_f32le") << QStringLiteral("-ar")
         << QString::number(format.sampleRate) << QStringLiteral("-ac")
         << QString::number(format.channels) << QStringLiteral("pipe:1");
    return args;
}

}
