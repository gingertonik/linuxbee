#include "converter.h"

#include "paths/pathbuilder.h"
#include "tagcopy.h"

#include <algorithm>
#include <optional>

#include <QByteArray>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRandomGenerator>
#include <QStringList>

namespace linuxbee {

namespace {

// C-CLEAN: the part file is ".lbpart-" + six characters chosen at random from
// a-z and 0-9 + "-" + the destination's own file name, in the destination's
// folder (".lbpart-k3x9qa-song.mp3"). A name that exists is passed over, so a
// user's file is never the part file.
QString partPathFor(const QString &destination)
{
    static constexpr char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    const int slash = destination.lastIndexOf(QLatin1Char('/'));
    const QString folder = destination.left(slash + 1);
    const QString name = destination.mid(slash + 1);
    for (;;) {
        QString chosen;
        for (int i = 0; i < 6; ++i)
            chosen += QLatin1Char(
                alphabet[QRandomGenerator::global()->bounded(int(sizeof alphabet) - 1)]);
        const QString path = folder + QStringLiteral(".lbpart-") + chosen + QLatin1Char('-') + name;
        const QFileInfo info(path);
        if (!info.exists() && !info.isSymLink())
            return path;
    }
}

// C-CLEAN: removes what an earlier conversion of this destination left when it
// was killed: files in the folder named exactly like a part file above. Nothing
// else is removed. The names are compared by hand: a music file's name carries
// [ ] * ? , which a name filter takes for wildcards.
void clearLeftovers(const QString &folder, const QString &fileName)
{
    const QString head = QStringLiteral(".lbpart-");
    const QString tail = QLatin1Char('-') + fileName;
    const QStringList names = QDir(folder).entryList(QDir::Files | QDir::Hidden, QDir::Name);
    for (const QString &name : names) {
        if (name.size() != head.size() + 6 + tail.size() || !name.startsWith(head)
            || !name.endsWith(tail))
            continue;
        const QString chosen = name.mid(head.size(), 6);
        if (!std::all_of(chosen.cbegin(), chosen.cend(), [](QChar c) {
                return (c.unicode() >= u'a' && c.unicode() <= u'z')
                    || (c.unicode() >= u'0' && c.unicode() <= u'9');
            }))
            continue;
        QFile::remove(folder + name);
    }
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

// C-STEPS 3: the path two files are compared by. The real path when the file
// exists; otherwise the real folder with the file name.
QString comparablePath(const QString &path)
{
    const QFileInfo info(path);
    const QString real = info.canonicalFilePath();
    if (!real.isEmpty())
        return real;
    const QString folder = QFileInfo(info.absolutePath()).canonicalFilePath();
    if (folder.isEmpty())
        return info.absoluteFilePath();
    return folder + QLatin1Char('/') + info.fileName();
}

// C-EXISTS: the destination exists already. Returns the result when the job
// ends here. Otherwise `destination` is the name the converted file goes to.
std::optional<ConvertResult> resolveExisting(
    const ConvertJob &job, const ReadResult &source, QString &destination)
{
    if (!QFileInfo::exists(destination))
        return std::nullopt;

    ConvertResult result;
    result.outputPath = destination;
    switch (job.ifExists) {
    case ConvertJob::IfExists::Skip:
        result.status = ConvertResult::Status::Skipped;
        return result;
    case ConvertJob::IfExists::UpdateTags:
        // ffmpeg does not run. The tags follow the source, on the existing file.
        if (!job.copyTags) {
            result.status = ConvertResult::Status::Skipped;
            return result;
        }
        {
            const QString error = convert::copyTags(
                source.tags, destination, job.copyPictures, job.tagOptions, &result.warnings);
            if (!error.isEmpty()) {
                result.status = ConvertResult::Status::Failed;
                result.message = error;
                return result;
            }
        }
        result.status = ConvertResult::Status::TagsUpdated;
        return result;
    case ConvertJob::IfExists::Rename: {
        const QString free = uniqueRelativePath(
            destination, [](const QString &path) { return QFileInfo::exists(path); });
        if (free.isEmpty())
            return withStatus(
                ConvertResult::Status::Failed, QStringLiteral("no free file name is left"));
        destination = free;
        return std::nullopt;
    }
    case ConvertJob::IfExists::Overwrite:
        // Replaced at step 9, once the new file is complete.
        return std::nullopt;
    }
    return std::nullopt;
}

// Step 9: the part file takes the destination's place. QFile::rename never
// replaces a file, so an existing destination is set aside first and put back
// when the move fails. A failed run leaves the old file as it was.
bool moveIntoPlace(const QString &partPath, const QString &destination)
{
    if (!QFileInfo::exists(destination))
        return QFile::rename(partPath, destination);
    // The old file goes aside under a name no file has. A file that already
    // has the name is someone else's, so it is never removed.
    const QString aside = uniqueRelativePath(destination + QStringLiteral(".replaced"),
        [](const QString &path) { return QFileInfo::exists(path); });
    if (aside.isEmpty() || !QFile::rename(destination, aside))
        return false;
    if (QFile::rename(partPath, destination)) {
        QFile::remove(aside);
        return true;
    }
    QFile::rename(aside, destination);
    return false;
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

    // Step 3: the source is never written, so a destination that is the source is refused.
    if (comparablePath(job.source) == comparablePath(job.destination))
        return withStatus(ConvertResult::Status::InvalidJob,
            QStringLiteral("the destination is the source file"));

    // Step 4: the destination exists already (C-EXISTS).
    QString destination = job.destination;
    if (std::optional<ConvertResult> done = resolveExisting(job, source, destination))
        return *done;

    // Step 5: C-PROGRESS starts at exactly 0, before anything is written.
    if (progress && !progress(0.0))
        return withStatus(ConvertResult::Status::Cancelled, QStringLiteral("cancelled"));

    // Step 6: the destination's folder.
    const QString folder = destination.left(destination.lastIndexOf(QLatin1Char('/')) + 1);
    if (!folder.isEmpty() && !QDir().mkpath(folder))
        return withStatus(
            ConvertResult::Status::Failed, QStringLiteral("could not create the folder"));

    // Step 7: ffmpeg writes the part file, under a name no file has. Only the
    // leftovers of a killed run of this destination are removed first.
    const QString partPath = partPathFor(destination);
    clearLeftovers(folder, QFileInfo(destination).fileName());

    QProcess ffmpeg;
    ffmpeg.setProgram(job.ffmpegPath);
    // Absolute paths: a name that starts with "-" or looks like "concat:x" is
    // never read by ffmpeg as an option or a protocol.
    ffmpeg.setArguments(ffmpegArguments(QFileInfo(job.source).absoluteFilePath(),
        QFileInfo(partPath).absoluteFilePath(), job.settings, source.tags.audio.bitsPerSample));
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
    // C-PROGRESS: restarted on every call. A second without one means the
    // encoder is silent, and the function is asked again with the last value.
    QElapsedTimer sinceCall;
    sinceCall.start();
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
            sinceCall.restart();
            if (progress && !progress(reported))
                cancelled = true;
        }
    };
    while (!cancelled && ffmpeg.state() != QProcess::NotRunning) {
        ffmpeg.waitForReadyRead(100);
        consume(ffmpeg.readAllStandardOutput());
        if (!cancelled && sinceCall.hasExpired(1000)) {
            sinceCall.restart();
            if (progress && !progress(reported))
                cancelled = true;
        }
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

    // Step 9: the move. The old file is replaced only here.
    if (!moveIntoPlace(partPath, destination)) {
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
    result.outputPath = destination;
    return result;
}

}
