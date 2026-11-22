#include "converter.h"

#include "paths/pathbuilder.h"
#include "tagcopy.h"

#include <algorithm>
#include <cerrno>
#include <optional>

#include <stdio.h>
#include <sys/stat.h>

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

// C-WHOLE: a length is "shorter" than `reference` only by more than this many
// milliseconds: 1000 from 2 s up, half the length from 0.5 s to 2 s, 250 below.
qint64 allowanceMs(qint64 reference)
{
    if (reference >= 2000)
        return 1000;
    if (reference >= 500)
        return reference / 2;
    return 250;
}

bool isShorter(qint64 length, qint64 reference)
{
    return length < reference - allowanceMs(reference);
}

// C-EXISTS: a name is there when the system says so of the name itself (lstat),
// not of what it leads to: a link that leads nowhere is there.
bool nameIsThere(const QString &path)
{
    struct stat info;
    return ::lstat(QFile::encodeName(path).constData(), &info) == 0;
}

// How a count ended: with a length (or -1 when it cannot be counted), asked to
// stop by the progress function, silent for longer than the job allows, or
// over the limit of what the part file can hold.
enum class Count { Counted, Cancelled, Silent, TooMuch };

// C-REPLACE 1: the sound of `path` counted by playing it through. The length
// in milliseconds, or -1 when it cannot be counted (not started, ended badly,
// no sample rate or no samples). The output is read as it comes: an hour of
// Opus is some 180,000 lines. The count is watched: the progress function is
// asked about once a second with `reported`, and ffprobe is killed and waited
// for when it says stop, has counted over `limitSamples` (0: no limit), or has
// counted no samples for `silenceMs`.
qint64 playedLengthMs(const QString &ffmpegPath, const QString &path, int silenceMs,
    qint64 limitSamples, const ConvertProgress &progress, double reported, Count &how)
{
    how = Count::Counted;
    QString program = QStringLiteral("ffprobe");
    const int slash = ffmpegPath.lastIndexOf(QLatin1Char('/'));
    if (slash >= 0) {
        const QFileInfo beside(ffmpegPath.left(slash + 1) + QStringLiteral("ffprobe"));
        if (beside.isFile())
            program = beside.absoluteFilePath();
    }

    QProcess probe;
    probe.setProgram(program);
    probe.setArguments({ QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-select_streams"), QStringLiteral("a:0"), QStringLiteral("-show_entries"),
        QStringLiteral("frame=nb_samples:stream=sample_rate"), QStringLiteral("-of"),
        QStringLiteral("default=nw=1"), QFileInfo(path).absoluteFilePath() });
    probe.setStandardErrorFile(QProcess::nullDevice());
    probe.start();
    if (!probe.waitForStarted())
        return -1;

    qint64 samples = 0;
    qint64 rate = 0;
    QByteArray pending;
    QElapsedTimer sinceOutput;
    sinceOutput.start();
    // Only a frame with samples breaks the silence; any other line, however
    // many, does not.
    const auto consume = [&](const QByteArray &chunk) {
        pending.append(chunk);
        for (int newline = pending.indexOf('\n'); newline >= 0; newline = pending.indexOf('\n')) {
            const QByteArray line = pending.left(newline).trimmed();
            pending.remove(0, newline + 1);
            bool ok = false;
            if (line.startsWith("nb_samples=")) {
                const qint64 n = line.mid(11).toLongLong(&ok);
                if (ok && n > 0) {
                    samples += n;
                    sinceOutput.restart();
                }
            } else if (line.startsWith("sample_rate=")) {
                const qint64 n = line.mid(12).toLongLong(&ok);
                if (ok && rate == 0)
                    rate = n;
            }
        }
    };
    const auto overLimit = [&]() { return limitSamples > 0 && samples > limitSamples; };
    QElapsedTimer sinceCall;
    sinceCall.start();
    while (probe.state() != QProcess::NotRunning) {
        probe.waitForReadyRead(100);
        consume(probe.readAllStandardOutput());
        if (sinceCall.hasExpired(1000)) {
            sinceCall.restart();
            if (progress && !progress(reported))
                how = Count::Cancelled;
        }
        if (how == Count::Counted && overLimit())
            how = Count::TooMuch;
        if (how == Count::Counted && probe.state() != QProcess::NotRunning
            && sinceOutput.hasExpired(silenceMs))
            how = Count::Silent;
        if (how != Count::Counted) {
            probe.kill();
            probe.waitForFinished(-1);
            return -1;
        }
    }
    probe.waitForFinished(-1);
    consume(probe.readAllStandardOutput() + "\n");
    if (overLimit()) {
        how = Count::TooMuch;
        return -1;
    }
    if (probe.exitStatus() != QProcess::NormalExit || probe.exitCode() != 0 || rate <= 0
        || samples <= 0)
        return -1;
    return samples * 1000 / rate;
}

// C-EXISTS: the destination exists already. Returns the result when the job
// ends here. Otherwise `destination` is the name the converted file goes to.
std::optional<ConvertResult> resolveExisting(
    const ConvertJob &job, const ReadResult &source, QString &destination)
{
    if (!nameIsThere(destination))
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
                source.tags, destination, job.copyPictures, job.tagOptions, &result.warnings, true);
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
            destination, [](const QString &path) { return nameIsThere(path); });
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

// C-PLACE: what was at the destination's name when C-EXISTS answered, known by
// what the system says it is: device and number, and the size, the time of the
// contents and the time of the record. Never by its name.
struct Seen {
    bool file = false;
    dev_t device = 0;
    ino_t number = 0;
    off_t size = 0;
    timespec contents { };
    timespec record { };
};

bool sameTime(const timespec &a, const timespec &b)
{
    return a.tv_sec == b.tv_sec && a.tv_nsec == b.tv_nsec;
}

Seen lookAt(const QString &destination)
{
    Seen seen;
    struct stat info;
    if (::lstat(QFile::encodeName(destination).constData(), &info) == 0) {
        seen.file = true;
        seen.device = info.st_dev;
        seen.number = info.st_ino;
        seen.size = info.st_size;
        seen.contents = info.st_mtim;
        seen.record = info.st_ctim;
    }
    return seen;
}

enum class Placed { Done, Taken, Changed, Gone, NoWay, Failed };

bool diskLacks(int error)
{
    return error == EINVAL || error == ENOSYS || error == ENOTSUP || error == EOPNOTSUPP;
}

// Step 9, nothing seen: the part file gets the name by a move that never
// replaces. Where the disk cannot do that, a second name and then the taking
// away of the part file's own. All three calls are the job's own; errno is read
// straight after the call that set it. Nothing that can replace is called.
Placed moveWithoutReplacing(
    const convert::PlaceIo &io, const QString &partPath, const QString &destination)
{
    const QByteArray from = QFile::encodeName(partPath);
    const QByteArray to = QFile::encodeName(destination);
    if (io.renameNoReplace(from.constData(), to.constData()) == 0)
        return Placed::Done;
    const int moveError = errno;
    if (moveError == EEXIST)
        return Placed::Taken;
    if (!diskLacks(moveError))
        return Placed::Failed;
    if (io.link(from.constData(), to.constData()) != 0) {
        const int linkError = errno;
        if (linkError == EEXIST)
            return Placed::Taken;
        if (linkError == EPERM || diskLacks(linkError))
            return Placed::NoWay;
        return Placed::Failed;
    }
    // The result has its name. If the part file's own name stays, that is a
    // second name of the same file, and the job is not failed for it.
    io.unlink(from.constData());
    return Placed::Done;
}

// Step 9, a file was seen (Overwrite): it is replaced only while the file at
// the name is still that file, and as it was.
Placed replaceSeen(const QString &partPath, const QString &destination, const Seen &seen)
{
    const Seen now = lookAt(destination);
    if (!now.file)
        return Placed::Gone;
    if (now.device != seen.device || now.number != seen.number)
        return Placed::Taken;
    if (now.size != seen.size || !sameTime(now.contents, seen.contents)
        || !sameTime(now.record, seen.record))
        return Placed::Changed;
    return ::rename(
               QFile::encodeName(partPath).constData(), QFile::encodeName(destination).constData())
            == 0
        ? Placed::Done
        : Placed::Failed;
}

// Step 9 (C-PLACE): the part file takes the destination's place and replaces
// only the file that was seen there. Keep both goes on to the next free name
// when a file has taken the one it chose; `destination` ends as the name used.
Placed moveIntoPlace(
    const ConvertJob &job, const QString &partPath, QString &destination, const Seen &seen)
{
    if (seen.file)
        return replaceSeen(partPath, destination, seen);
    QStringList tried;
    for (;;) {
        const Placed placed = moveWithoutReplacing(job.placeIo, partPath, destination);
        if (placed != Placed::Taken || job.ifExists != ConvertJob::IfExists::Rename)
            return placed;
        tried.append(destination);
        const QString next = uniqueRelativePath(job.destination,
            [&tried](const QString &path) { return nameIsThere(path) || tried.contains(path); });
        if (next.isEmpty())
            return Placed::Failed;
        destination = next;
    }
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

    // C-PLACE: what is at the name is looked at once, here (by the name itself,
    // not by what it leads to). Only Overwrite onto a file that is there has a
    // file seen; every other job saw nothing.
    // C-REPLACE: with Overwrite onto a file that is there, the length of that
    // file as readTags gives it (0 when it is not music).
    const Seen seen
        = job.ifExists == ConvertJob::IfExists::Overwrite ? lookAt(destination) : Seen();
    const bool replaces = seen.file;
    qint64 oldLengthMs = 0;
    if (replaces) {
        ReadOptions oldOptions;
        oldOptions.pictures = false;
        const ReadResult old = readTags(destination, oldOptions);
        if (old.status == ReadResult::Status::Ok)
            oldLengthMs = old.tags.audio.durationMs;
    }

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

    // C-WHOLE: the encoder is not taken at its word. The part file is read
    // whether or not tags are copied; one that cannot be read, or that is more
    // than a second shorter than a source of known length, is refused. Its own
    // length and rate are what the count of C-REPLACE is limited by.
    qint64 madeLengthMs = 0;
    qint64 madeRateHz = 0;
    {
        ReadOptions lookOptions;
        lookOptions.pictures = false;
        const ReadResult made = readTags(partPath, lookOptions);
        if (made.status != ReadResult::Status::Ok) {
            QFile::remove(partPath);
            return withStatus(ConvertResult::Status::Failed,
                QStringLiteral("the converted file could not be read"));
        }
        if (source.tags.audio.durationMs > 0
            && isShorter(made.tags.audio.durationMs, source.tags.audio.durationMs)) {
            QFile::remove(partPath);
            return withStatus(ConvertResult::Status::Failed,
                QStringLiteral("the converted file is shorter than the source"));
        }
        madeLengthMs = made.tags.audio.durationMs;
        madeRateHz = made.tags.audio.sampleRateHz;
    }

    // C-REPLACE: what replaces a file is played through first.
    if (replaces) {
        Count how = Count::Counted;
        // More samples than ten times the length and a minute, at the header's
        // rate, is a count that does not end. No limit when the rate is 0.
        const qint64 limitSamples
            = madeRateHz > 0 ? (10 * madeLengthMs + 60000) * madeRateHz / 1000 : 0;
        const qint64 played = playedLengthMs(
            job.ffmpegPath, partPath, job.countSilenceMs, limitSamples, progress, reported, how);
        if (how == Count::Cancelled) {
            QFile::remove(partPath);
            return withStatus(ConvertResult::Status::Cancelled, QStringLiteral("cancelled"));
        }
        if (how == Count::Silent) {
            QFile::remove(partPath);
            return withStatus(ConvertResult::Status::Failed,
                QStringLiteral("the converted file could not be checked: ffprobe gave no answer"));
        }
        if (how == Count::TooMuch) {
            QFile::remove(partPath);
            return withStatus(ConvertResult::Status::Failed,
                QStringLiteral("the converted file could not be checked: ffprobe counted far "
                               "more than the file's length"));
        }
        if (played < 0) {
            QFile::remove(partPath);
            return withStatus(ConvertResult::Status::Failed,
                QStringLiteral("the converted file could not be played through to check it"));
        }
        if (source.tags.audio.durationMs > 0 && isShorter(played, source.tags.audio.durationMs)) {
            QFile::remove(partPath);
            return withStatus(ConvertResult::Status::Failed,
                QStringLiteral("the converted file is shorter than the source"));
        }
        if (oldLengthMs > 0 && isShorter(played, oldLengthMs)) {
            QFile::remove(partPath);
            return withStatus(ConvertResult::Status::Failed,
                QStringLiteral("the converted file is shorter than the file it would replace; "
                               "remove that file first if this is meant"));
        }
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

    // Step 9: the move. The file that was seen is replaced only here, and only
    // while it is still that file (C-PLACE).
    const Placed placed = moveIntoPlace(job, partPath, destination, seen);
    if (placed != Placed::Done) {
        QFile::remove(partPath);
        result.status = ConvertResult::Status::Failed;
        switch (placed) {
        case Placed::Taken:
            result.message = QStringLiteral(
                "a file appeared at the destination while converting; it was not replaced");
            break;
        case Placed::Changed:
            result.message = QStringLiteral(
                "the file at the destination was changed while converting; it was not replaced");
            break;
        case Placed::Gone:
            result.message = QStringLiteral("the file at the destination was removed while "
                                            "converting; the converted file was not put there");
            break;
        case Placed::NoWay:
            result.message
                = QStringLiteral("the converted file could not be put in place: this disk can "
                                 "neither move a file without replacing one nor give it a second "
                                 "name");
            break;
        case Placed::Failed:
        case Placed::Done:
            result.message = QStringLiteral("could not move the converted file into place");
            break;
        }
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
