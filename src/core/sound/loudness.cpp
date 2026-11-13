#include "replaygain.h"

#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

#include <cmath>

namespace linuxbee {

namespace {

// ffmpeg prints -inf for silence and for a peak it cannot compute.
bool isInfinite(const QString &text)
{
    return text.contains(QLatin1String("inf"), Qt::CaseInsensitive);
}

// The file: protocol makes ffmpeg read a name that begins with '-' or holds
// ':' as a plain file, never as an option or another protocol.
QString asInput(const QString &file)
{
    return QStringLiteral("file:") + file;
}

// The last line ffmpeg printed, for a reason the user can read.
QString lastLine(const QString &text)
{
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    return lines.isEmpty() ? QStringLiteral("no output") : lines.last().trimmed();
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

// N-MEASURE: ffmpeg runs with an argument list, never through a shell. Several
// files are brought to one rate and layout and joined, so an album is measured
// as one programme.
Loudness measureLoudness(const QStringList &files, const QString &ffmpegProgram)
{
    Loudness fail;
    if (files.isEmpty()) {
        fail.error = QStringLiteral("no file to measure");
        return fail;
    }
    for (const QString &file : files) {
        if (!QFileInfo(file).isFile()) {
            fail.error = QStringLiteral("not a file: %1").arg(file);
            return fail;
        }
    }

    QStringList args { QStringLiteral("-nostdin"), QStringLiteral("-hide_banner") };
    QString graph;
    for (qsizetype i = 0; i < files.size(); ++i) {
        args << QStringLiteral("-i") << asInput(files.at(i));
        graph += QStringLiteral("[%1:a:0]aresample=48000,aformat=channel_layouts=stereo[a%1];")
                     .arg(i);
    }
    for (qsizetype i = 0; i < files.size(); ++i)
        graph += QStringLiteral("[a%1]").arg(i);
    graph
        += QStringLiteral("concat=n=%1:v=0:a=1,ebur128=peak=true:framelog=quiet").arg(files.size());
    args << QStringLiteral("-filter_complex") << graph << QStringLiteral("-f")
         << QStringLiteral("null") << QStringLiteral("-");

    QProcess ffmpeg;
    ffmpeg.start(ffmpegProgram, args);
    if (!ffmpeg.waitForStarted()) {
        fail.error = QStringLiteral("cannot start ffmpeg: %1").arg(ffmpeg.errorString());
        return fail;
    }
    ffmpeg.waitForFinished(-1);

    // ffmpeg prints its summary on standard error.
    const QString output = QString::fromUtf8(ffmpeg.readAllStandardError());
    if (ffmpeg.exitStatus() != QProcess::NormalExit || ffmpeg.exitCode() != 0) {
        fail.error = QStringLiteral("ffmpeg could not read the audio: %1").arg(lastLine(output));
        return fail;
    }
    return parseLoudnessSummary(output);
}

}
