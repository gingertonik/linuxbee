#include "converter.h"

#include "tagcopy.h"

#include <algorithm>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStringList>

namespace linuxbee {

namespace {

// C-CLEAN: the part file is the destination's name with ".part" before its
// extension ("song.part.mp3"; "song.part" when there is no extension).
QString partPathFor(const QString &destination)
{
    const int slash = destination.lastIndexOf(QLatin1Char('/'));
    const QString folder = destination.left(slash + 1);
    const QString name = destination.mid(slash + 1);
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot > 0)
        return folder + name.left(dot) + QStringLiteral(".part") + name.mid(dot);
    return folder + name + QStringLiteral(".part");
}

// C-ENCODER: the last non-empty line ffmpeg wrote to standard error, trimmed.
QString encoderMessage(const QByteArray &standardError)
{
    const QStringList lines = QString::fromUtf8(standardError).split(QLatin1Char('\n'));
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        const QString line = it->trimmed();
        if (!line.isEmpty())
            return line;
    }
    return QStringLiteral("ffmpeg failed");
}

ConvertResult withStatus(ConvertResult::Status status, const QString &message)
{
    ConvertResult result;
    result.status = status;
    result.message = message;
    return result;
}

}

ConvertResult convertFile(const ConvertJob &job, const ConvertProgress &progress)
{
    // Steps 1 and 2: the settings, the destination, then the source.
    QStringList problems = validateSettings(job.settings);
    if (job.destination.isEmpty())
        problems.append(QStringLiteral("no destination file"));
    if (!problems.isEmpty())
        return withStatus(ConvertResult::Status::InvalidJob, problems.join(QStringLiteral("; ")));

    const ReadResult source = readTags(job.source);
    if (source.status == ReadResult::Status::NotFound)
        return withStatus(
            ConvertResult::Status::SourceNotFound, QStringLiteral("source not found"));
    if (source.status == ReadResult::Status::NotAudio)
        return withStatus(
            ConvertResult::Status::SourceNotAudio, QStringLiteral("source is not an audio file"));

    // Step 5: C-PROGRESS starts at exactly 0, before anything is written.
    if (progress && !progress(0.0))
        return withStatus(ConvertResult::Status::Cancelled, QStringLiteral("cancelled"));

    // Step 6: the destination's folder.
    const QString folder = job.destination.left(job.destination.lastIndexOf(QLatin1Char('/')) + 1);
    if (!folder.isEmpty() && !QDir().mkpath(folder))
        return withStatus(
            ConvertResult::Status::Failed, QStringLiteral("could not create the folder"));

    // Step 7: ffmpeg writes the part file. An old part file of that name goes first.
    const QString partPath = partPathFor(job.destination);
    QFile::remove(partPath);

    QProcess ffmpeg;
    ffmpeg.setProgram(job.ffmpegPath);
    ffmpeg.setArguments(
        ffmpegArguments(job.source, partPath, job.settings, source.tags.audio.bitsPerSample));
    ffmpeg.start();
    if (!ffmpeg.waitForStarted())
        return withStatus(
            ConvertResult::Status::EncoderMissing, QStringLiteral("ffmpeg could not be started"));

    // ffmpeg prints "out_time_us=N" lines on standard output while it works.
    // C-PROGRESS: N over the source's length, never below the last report and
    // never above 0.99; the 1 is reported after the move.
    const double totalUs = source.tags.audio.durationMs * 1000.0;
    QByteArray pending;
    double reported = 0.0;
    bool cancelled = false;
    const auto consume = [&](const QByteArray &chunk) {
        pending.append(chunk);
        for (int newline = pending.indexOf('\n'); newline >= 0 && !cancelled;
            newline = pending.indexOf('\n')) {
            const QByteArray line = pending.left(newline).trimmed();
            pending.remove(0, newline + 1);
            if (!line.startsWith("out_time_us=") || totalUs <= 0)
                continue;
            bool ok = false;
            const qint64 us = line.mid(12).toLongLong(&ok);
            if (!ok)
                continue;
            reported = std::max(reported, std::min(0.99, static_cast<double>(us) / totalUs));
            if (progress && !progress(reported))
                cancelled = true;
        }
    };
    while (!cancelled && ffmpeg.state() != QProcess::NotRunning) {
        ffmpeg.waitForReadyRead(100);
        consume(ffmpeg.readAllStandardOutput());
    }
    if (!cancelled)
        consume(ffmpeg.readAllStandardOutput());

    if (cancelled) {
        ffmpeg.kill();
        ffmpeg.waitForFinished(-1);
        QFile::remove(partPath);
        return withStatus(ConvertResult::Status::Cancelled, QStringLiteral("cancelled"));
    }
    ffmpeg.waitForFinished(-1);
    if (ffmpeg.exitStatus() != QProcess::NormalExit || ffmpeg.exitCode() != 0) {
        QFile::remove(partPath);
        return withStatus(
            ConvertResult::Status::Failed, encoderMessage(ffmpeg.readAllStandardError()));
    }

    // Step 8: the tags go onto the part file, before it is moved.
    ConvertResult result;
    if (job.copyTags) {
        const QString error = convert::copyTags(
            source.tags, partPath, job.copyPictures, job.tagOptions, &result.warnings);
        if (!error.isEmpty()) {
            QFile::remove(partPath);
            result.status = ConvertResult::Status::Failed;
            result.message = error;
            return result;
        }
    }

    // Step 9: the part file takes the destination's place. QFile::rename does
    // not replace an existing file, so the old one is left alone when the
    // rename fails.
    if (!QFile::rename(partPath, job.destination)) {
        QFile::remove(partPath);
        result.status = ConvertResult::Status::Failed;
        result.message = QStringLiteral("could not move the converted file into place");
        return result;
    }
    // The file is in place. The final report cannot undo that, so the answer
    // to it is not used.
    if (progress)
        progress(1.0);
    result.status = ConvertResult::Status::Converted;
    result.outputPath = job.destination;
    return result;
}

}
