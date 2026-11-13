#include "format.h"

#include <QChar>
#include <QHash>

namespace linuxbee::fieldformat {

QString duration(qint64 milliseconds)
{
    const qint64 ms = milliseconds < 0 ? 0 : milliseconds;
    const qint64 seconds = (ms + 500) / 1000;
    const qint64 hours = seconds / 3600;
    const qint64 minutes = (seconds % 3600) / 60;
    const qint64 secs = seconds % 60;

    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(secs, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(minutes).arg(secs, 2, 10, QLatin1Char('0'));
}

QString kind(const QString &format)
{
    static const QHash<QString, QString> known = {
        { QStringLiteral("mp3"), QStringLiteral("MPEG audio file") },
        { QStringLiteral("flac"), QStringLiteral("FLAC audio file") },
        { QStringLiteral("ogg"), QStringLiteral("Ogg Vorbis audio file") },
        { QStringLiteral("opus"), QStringLiteral("Opus audio file") },
        { QStringLiteral("m4a"), QStringLiteral("MPEG-4 audio file") },
        { QStringLiteral("wav"), QStringLiteral("WAV audio file") },
        { QStringLiteral("wv"), QStringLiteral("WavPack audio file") },
    };
    const auto it = known.constFind(format);
    if (it != known.constEnd())
        return *it;
    return format.toUpper() + QStringLiteral(" audio file");
}

QString sampleRate(int hertz)
{
    QString text = QString::number(hertz / 1000.0, 'f', 3);
    while (text.endsWith(QLatin1Char('0')))
        text.chop(1);
    if (text.endsWith(QLatin1Char('.')))
        text.chop(1);
    return text + QStringLiteral(" kHz");
}

}
