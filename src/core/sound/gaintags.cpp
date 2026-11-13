#include "replaygain.h"

#include <QRegularExpression>

namespace linuxbee {

namespace {

// The first value of a key, the key matched without regard to case.
std::optional<QString> firstValue(const QMap<QString, QStringList> &other, const QString &key)
{
    for (auto it = other.cbegin(); it != other.cend(); ++it) {
        if (it.key().compare(key, Qt::CaseInsensitive) == 0 && !it.value().isEmpty())
            return it.value().first();
    }
    return std::nullopt;
}

// N-TAGS: a decimal number, optional sign, optional dB in any case, white
// space ignored around it.
std::optional<double> parseGainDb(const QString &text)
{
    static const QRegularExpression re(
        QStringLiteral(R"(^\s*([+-]?(?:[0-9]+\.?[0-9]*|\.[0-9]+))\s*(?:db)?\s*$)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch())
        return std::nullopt;
    bool ok = false;
    const double value = m.captured(1).toDouble(&ok);
    return ok ? std::optional<double>(value) : std::nullopt;
}

// N-TAGS: a decimal number above zero, with no unit.
std::optional<double> parsePeak(const QString &text)
{
    static const QRegularExpression re(
        QStringLiteral(R"(^\s*([+-]?(?:[0-9]+\.?[0-9]*|\.[0-9]+))\s*$)"));
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch())
        return std::nullopt;
    bool ok = false;
    const double value = m.captured(1).toDouble(&ok);
    if (!ok || value <= 0.0)
        return std::nullopt;
    return value;
}

// N-R128: a whole number from -32768 to 32767 in 1/256 dB relative to
// -23 LUFS. ReplayGain's reference is 5 dB louder.
std::optional<double> parseR128Db(const QString &text)
{
    static const QRegularExpression re(QStringLiteral(R"(^\s*([+-]?[0-9]+)\s*$)"));
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch())
        return std::nullopt;
    bool ok = false;
    const qlonglong raw = m.captured(1).toLongLong(&ok);
    if (!ok || raw < -32768 || raw > 32767)
        return std::nullopt;
    return static_cast<double>(raw) / 256.0 + 5.0;
}

// A REPLAYGAIN_* gain wins; an R128_* gain is used when that one is missing
// or cannot be read.
std::optional<double> gainOf(
    const QMap<QString, QStringList> &other, const QString &replayGainKey, const QString &r128Key)
{
    if (const auto text = firstValue(other, replayGainKey)) {
        if (const auto db = parseGainDb(*text))
            return db;
    }
    if (const auto text = firstValue(other, r128Key))
        return parseR128Db(*text);
    return std::nullopt;
}

std::optional<double> peakOf(const QMap<QString, QStringList> &other, const QString &key)
{
    if (const auto text = firstValue(other, key))
        return parsePeak(*text);
    return std::nullopt;
}

}

GainValues gainFromTags(const TrackTags &tags)
{
    GainValues v;
    v.trackGainDb = gainOf(
        tags.other, QStringLiteral("REPLAYGAIN_TRACK_GAIN"), QStringLiteral("R128_TRACK_GAIN"));
    v.albumGainDb = gainOf(
        tags.other, QStringLiteral("REPLAYGAIN_ALBUM_GAIN"), QStringLiteral("R128_ALBUM_GAIN"));
    v.trackPeak = peakOf(tags.other, QStringLiteral("REPLAYGAIN_TRACK_PEAK"));
    v.albumPeak = peakOf(tags.other, QStringLiteral("REPLAYGAIN_ALBUM_PEAK"));
    return v;
}

}
