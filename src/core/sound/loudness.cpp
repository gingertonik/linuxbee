#include "replaygain.h"

#include <QRegularExpression>

#include <cmath>

namespace linuxbee {

namespace {

// ffmpeg prints -inf for silence and for a peak it cannot compute.
bool isInfinite(const QString &text)
{
    return text.contains(QLatin1String("inf"), Qt::CaseInsensitive);
}

}

// N-REFERENCE: gain = -18 - loudness (RG2).
double Loudness::gainDb() const
{
    return -18.0 - integratedLufs;
}

// N-REFERENCE: the peak as a linear value, 10^(dBFS / 20).
double Loudness::peak() const
{
    return std::pow(10.0, truePeakDbfs / 20.0);
}

// N-PARSE: only the text from the last "Summary:" counts.
Loudness parseLoudnessSummary(const QString &ffmpegOutput)
{
    Loudness l;
    const qsizetype at = ffmpegOutput.lastIndexOf(QStringLiteral("Summary:"));
    if (at < 0) {
        l.error = QStringLiteral("no loudness summary in the output");
        return l;
    }
    const QString summary = ffmpegOutput.mid(at);

    static const QRegularExpression integratedRe(QStringLiteral(R"(\bI:\s*(\S+)\s+LUFS)"));
    static const QRegularExpression peakRe(QStringLiteral(R"(Peak:\s*(\S+)\s+dBFS)"));

    const QRegularExpressionMatch im = integratedRe.match(summary);
    if (!im.hasMatch()) {
        l.error = QStringLiteral("no integrated loudness in the summary");
        return l;
    }
    const QRegularExpressionMatch pm = peakRe.match(summary);
    if (!pm.hasMatch()) {
        l.error = QStringLiteral("no true peak in the summary");
        return l;
    }

    // Silence, or too short to measure: -inf or -70 LUFS and below.
    const QString integratedText = im.captured(1);
    double integrated = 0.0;
    if (isInfinite(integratedText)) {
        l.error = QStringLiteral("too quiet or too short to measure (loudness -inf)");
        return l;
    }
    bool ok = false;
    integrated = integratedText.toDouble(&ok);
    if (!ok) {
        l.error = QStringLiteral("unreadable integrated loudness: %1").arg(integratedText);
        return l;
    }
    if (integrated <= -70.0) {
        l.error
            = QStringLiteral("too quiet or too short to measure (loudness at or below -70 LUFS)");
        return l;
    }

    // A peak of -inf with a real loudness is read as -120 dBFS.
    const QString peakText = pm.captured(1);
    double peakDbfs = -120.0;
    if (!isInfinite(peakText)) {
        peakDbfs = peakText.toDouble(&ok);
        if (!ok) {
            l.error = QStringLiteral("unreadable true peak: %1").arg(peakText);
            return l;
        }
    }

    l.ok = true;
    l.integratedLufs = integrated;
    l.truePeakDbfs = peakDbfs;
    return l;
}

}
