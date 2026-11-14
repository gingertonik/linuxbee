#include "replaygain.h"

#include <QFileInfo>
#include <QList>
#include <QProcess>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace linuxbee {

namespace {

// A plain decimal number, as ffmpeg prints one: an optional sign, digits and
// an optional fraction. Words (nan, inf) and exponents are not numbers here,
// and a value too large to hold is not finite.
std::optional<double> finiteDecimal(const QString &text)
{
    static const QRegularExpression re(QStringLiteral(R"(^[+-]?(?:[0-9]+\.?[0-9]*|\.[0-9]+)$)"));
    if (!re.match(text).hasMatch())
        return std::nullopt;
    bool ok = false;
    const double value = text.toDouble(&ok);
    if (!ok || !std::isfinite(value))
        return std::nullopt;
    return value;
}

// N-PARSE: a true peak of -inf is digital silence, read as -120 dBFS.
std::optional<double> truePeakDbfs(const QString &text)
{
    if (text.compare(QStringLiteral("-inf"), Qt::CaseInsensitive) == 0)
        return -120.0;
    return finiteDecimal(text);
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

// Runs ffmpeg with an argument list and returns what it wrote to standard
// error. On failure the reason goes to error and the result is false.
bool runFfmpeg(
    const QStringList &args, const QString &ffmpegProgram, QString &output, QString &error)
{
    QProcess ffmpeg;
    ffmpeg.start(ffmpegProgram, args);
    if (!ffmpeg.waitForStarted()) {
        error = QStringLiteral("cannot start ffmpeg: %1").arg(ffmpeg.errorString());
        return false;
    }
    ffmpeg.waitForFinished(-1);

    // ffmpeg prints its summary, and the filters their reports, on standard error.
    output = QString::fromUtf8(ffmpeg.readAllStandardError());
    if (ffmpeg.exitStatus() != QProcess::NormalExit || ffmpeg.exitCode() != 0) {
        error = QStringLiteral("ffmpeg could not read the audio: %1").arg(lastLine(output));
        return false;
    }
    return true;
}

// ffmpeg's ebur128 on one file, as it is. Nothing is put before the filter.
QStringList singleFileArgs(const QString &file)
{
    return { QStringLiteral("-nostdin"), QStringLiteral("-hide_banner"), QStringLiteral("-i"),
        asInput(file), QStringLiteral("-filter_complex"),
        QStringLiteral("[0:a:0]ebur128=peak=true:framelog=quiet"), QStringLiteral("-f"),
        QStringLiteral("null"), QStringLiteral("-") };
}

// The sample rate, channel count and channel layout of a file's first audio
// stream, as ffmpeg's ashowinfo prints them for one frame.
struct Format {
    int rate = 0;
    int channels = 0;
    QString layout;
};

bool probeFormat(const QString &file, const QString &ffmpegProgram, Format &format, QString &error)
{
    const QStringList args { QStringLiteral("-nostdin"), QStringLiteral("-hide_banner"),
        QStringLiteral("-i"), asInput(file), QStringLiteral("-map"), QStringLiteral("0:a:0"),
        QStringLiteral("-af"), QStringLiteral("ashowinfo"), QStringLiteral("-frames:a"),
        QStringLiteral("1"), QStringLiteral("-f"), QStringLiteral("null"), QStringLiteral("-") };
    QString output;
    if (!runFfmpeg(args, ffmpegProgram, output, error))
        return false;

    static const QRegularExpression rateRe(QStringLiteral(R"(\brate:(\d+))"));
    static const QRegularExpression channelsRe(QStringLiteral(R"(\bchannels:(\d+))"));
    static const QRegularExpression layoutRe(QStringLiteral(R"(\bchlayout:(\S+))"));
    const QRegularExpressionMatch rm = rateRe.match(output);
    const QRegularExpressionMatch cm = channelsRe.match(output);
    const QRegularExpressionMatch lm = layoutRe.match(output);
    if (!rm.hasMatch() || !cm.hasMatch() || !lm.hasMatch()) {
        error = QStringLiteral("cannot read the rate and channel layout of %1").arg(file);
        return false;
    }
    format.rate = rm.captured(1).toInt();
    format.channels = cm.captured(1).toInt();
    format.layout = lm.captured(1);
    return true;
}

// N-PARSE: the integrated loudness and the true peak as ffmpeg wrote them,
// from the last "Summary:" on. Neither is judged here.
bool summaryTexts(const QString &ffmpegOutput, QString &integrated, QString &peak, QString &error)
{
    const qsizetype at = ffmpegOutput.lastIndexOf(QStringLiteral("Summary:"));
    if (at < 0) {
        error = QStringLiteral("no loudness summary in the output");
        return false;
    }
    const QString summary = ffmpegOutput.mid(at);

    static const QRegularExpression integratedRe(QStringLiteral(R"(\bI:\s*(\S+)\s+LUFS)"));
    static const QRegularExpression peakRe(QStringLiteral(R"(Peak:\s*(\S+)\s+dBFS)"));

    const QRegularExpressionMatch im = integratedRe.match(summary);
    if (!im.hasMatch()) {
        error = QStringLiteral("no integrated loudness in the summary");
        return false;
    }
    const QRegularExpressionMatch pm = peakRe.match(summary);
    if (!pm.hasMatch()) {
        error = QStringLiteral("no true peak in the summary");
        return false;
    }
    integrated = im.captured(1);
    peak = pm.captured(1);
    return true;
}

// N-MEASURE, several files: each file's own peak, measured as it is, and the
// largest of them. The peak is never that of a converted signal.
bool albumPeakDbfs(
    const QStringList &files, const QString &ffmpegProgram, double &peak, QString &error)
{
    std::optional<double> largest;
    for (const QString &file : files) {
        QString output;
        if (!runFfmpeg(singleFileArgs(file), ffmpegProgram, output, error))
            return false;
        QString integrated;
        QString peakText;
        if (!summaryTexts(output, integrated, peakText, error))
            return false;
        const std::optional<double> own = truePeakDbfs(peakText);
        if (!own) {
            error = QStringLiteral("unreadable true peak in %1: %2").arg(file, peakText);
            return false;
        }
        if (!largest || *own > *largest)
            largest = own;
    }
    peak = *largest;
    return true;
}

// N-MEASURE, several files: the join. The files are brought to one rate and
// one layout only when they differ: the highest rate, and the layout of the
// file with the most channels (ties: the name that sorts first).
Loudness measureAlbum(const QStringList &files, const QString &ffmpegProgram)
{
    Loudness fail;

    double peak = 0.0;
    if (!albumPeakDbfs(files, ffmpegProgram, peak, fail.error))
        return fail;

    QList<Format> formats;
    for (const QString &file : files) {
        Format format;
        if (!probeFormat(file, ffmpegProgram, format, fail.error))
            return fail;
        formats.append(format);
    }

    int rate = 0;
    Format widest = formats.first();
    for (const Format &format : formats) {
        rate = std::max(rate, format.rate);
        if (format.channels > widest.channels
            || (format.channels == widest.channels && format.layout < widest.layout))
            widest = format;
    }
    const QString layout = widest.layout;

    QStringList args { QStringLiteral("-nostdin"), QStringLiteral("-hide_banner") };
    QString graph;
    for (qsizetype i = 0; i < files.size(); ++i) {
        args << QStringLiteral("-i") << asInput(files.at(i));
        const Format &format = formats.at(i);
        const QString in = QStringLiteral("[%1:a:0]").arg(i);
        if (format.rate == rate && format.layout == layout)
            graph += in + QStringLiteral("anull[a%1];").arg(i);
        else
            graph += in
                + QStringLiteral("aresample=%1,aformat=channel_layouts=%2[a%3];")
                      .arg(rate)
                      .arg(layout)
                      .arg(i);
    }
    for (qsizetype i = 0; i < files.size(); ++i)
        graph += QStringLiteral("[a%1]").arg(i);
    graph
        += QStringLiteral("concat=n=%1:v=0:a=1,ebur128=peak=true:framelog=quiet").arg(files.size());
    args << QStringLiteral("-filter_complex") << graph << QStringLiteral("-f")
         << QStringLiteral("null") << QStringLiteral("-");

    QString output;
    if (!runFfmpeg(args, ffmpegProgram, output, fail.error))
        return fail;
    Loudness album = parseLoudnessSummary(output);
    if (!album.ok)
        return album;
    album.truePeakDbfs = peak;
    return album;
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
// N-FINITE: a loudness or peak that is not a finite number is not ok.
Loudness parseLoudnessSummary(const QString &ffmpegOutput)
{
    Loudness l;
    QString integratedText;
    QString peakText;
    if (!summaryTexts(ffmpegOutput, integratedText, peakText, l.error))
        return l;

    // Silence, or too short to measure: -inf, or -70 LUFS and below.
    if (integratedText.compare(QStringLiteral("-inf"), Qt::CaseInsensitive) == 0) {
        l.error = QStringLiteral("too quiet or too short to measure (loudness -inf)");
        return l;
    }
    const std::optional<double> integrated = finiteDecimal(integratedText);
    if (!integrated) {
        l.error = QStringLiteral("unreadable integrated loudness: %1").arg(integratedText);
        return l;
    }
    if (*integrated <= -70.0) {
        l.error
            = QStringLiteral("too quiet or too short to measure (loudness at or below -70 LUFS)");
        return l;
    }

    const std::optional<double> peakDbfs = truePeakDbfs(peakText);
    if (!peakDbfs) {
        l.error = QStringLiteral("unreadable true peak: %1").arg(peakText);
        return l;
    }

    l.ok = true;
    l.integratedLufs = *integrated;
    l.truePeakDbfs = *peakDbfs;
    return l;
}

// N-MEASURE: ffmpeg runs with an argument list, never through a shell. One
// file is measured as it is. Several files are an album: joined as one
// programme, with the peak of the files' own.
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

    if (files.size() == 1) {
        QString output;
        if (!runFfmpeg(singleFileArgs(files.first()), ffmpegProgram, output, fail.error))
            return fail;
        return parseLoudnessSummary(output);
    }
    return measureAlbum(files, ffmpegProgram);
}

}
