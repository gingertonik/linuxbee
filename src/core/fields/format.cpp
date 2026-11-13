#include "format.h"

#include <QChar>
#include <QHash>

namespace linuxbee::fieldformat {

namespace {

bool isAsciiDigit(QChar c)
{
    return c >= QLatin1Char('0') && c <= QLatin1Char('9');
}

}

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

QString padWholeNumber(const QString &text, int width)
{
    if (text.isEmpty())
        return text;
    for (const QChar c : text) {
        if (!isAsciiDigit(c))
            return text;
    }

    // Drop leading zeros, but keep one digit so "000" stays "0".
    qsizetype start = 0;
    while (start < text.size() - 1 && text.at(start) == QLatin1Char('0'))
        ++start;
    QString digits = text.mid(start);
    if (digits.size() < width)
        digits.prepend(QString(width - digits.size(), QLatin1Char('0')));
    return digits;
}

QString fourDigitYear(const QString &text)
{
    const qsizetype size = text.size();
    qsizetype i = 0;
    while (i < size) {
        if (!isAsciiDigit(text.at(i))) {
            ++i;
            continue;
        }
        qsizetype end = i;
        while (end < size && isAsciiDigit(text.at(end)))
            ++end;
        if (end - i == 4)
            return text.mid(i, 4);
        i = end;
    }
    return { };
}

}
