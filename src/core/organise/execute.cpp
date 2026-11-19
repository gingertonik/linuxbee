#include "organiser.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <sys/stat.h>
#include <unistd.h>

#include <utility>

#include "aftermath.h"
#include "files/trash.h"
#include "transfer.h"

namespace linuxbee {

namespace {

// O-EXECUTE: the status of an entry that is not Ready, in words.
QString skipMessage(PlannedMove::Status status, const QStringList &missingFields)
{
    switch (status) {
    case PlannedMove::Status::AlreadyInPlace:
        return QStringLiteral("already in place");
    case PlannedMove::Status::MissingTags:
        return QStringLiteral("missing tags: %1").arg(missingFields.join(QStringLiteral(", ")));
    case PlannedMove::Status::EmptyName:
        return QStringLiteral("the template leaves no file name");
    case PlannedMove::Status::InvalidTemplate:
        return QStringLiteral("the template is not valid");
    case PlannedMove::Status::NotFound:
        return QStringLiteral("not found");
    case PlannedMove::Status::NotAudio:
        return QStringLiteral("not a music file");
    case PlannedMove::Status::Ready:
        break;
    }
    return { };
}

// A folder beside the target that no folder has yet, where the file being
// replaced waits while the newcomer goes in. It is named so that nothing shows
// it to the user: see recordTargetPath.
QString waitingFolder(const QString &target)
{
    const QFileInfo info(target);
    for (int n = 1; n <= 9999; ++n) {
        const QString candidate = info.absolutePath() + QLatin1Char('/')
            + QStringLiteral(".linuxbee-replaced-%1").arg(n);
        if (!QFileInfo::exists(candidate))
            return candidate;
    }
    return { };
}

// O-REPLACED: the trash record of a file sent from the waiting folder names
// that folder as the place it came from. Rewrites its Path to the target's
// folder, which is the waiting folder without its name. False when the record
// cannot be rewritten.
bool recordTargetPath(const QString &inTrash, const QString &waitingName)
{
    const QFileInfo trashed(inTrash);
    const QString record
        = trashed.dir().filePath(QStringLiteral("../info/%1.trashinfo").arg(trashed.fileName()));
    QFile file(record);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QByteArray text = file.readAll();
    file.close();
    text.replace(QByteArray("/") + waitingName.toUtf8() + QByteArray("/"), QByteArray("/"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(text) == text.size();
}

using FileId = std::pair<quint64, quint64>;

// The device and inode of what a path names (links followed), or false.
bool fileIdOf(const QString &path, FileId *id)
{
    struct stat st { };
    if (::stat(QFile::encodeName(path).constData(), &st) != 0)
        return false;
    *id = { static_cast<quint64>(st.st_dev), static_cast<quint64>(st.st_ino) };
    return true;
}

// O-EXECUTE, a link and its own file in one run: true when the link at `source`, with its
// text read from the folder of `target`, leads to the file whose identity is `file`.
bool linkLeadsTo(const QString &source, const QString &target, const FileId &file)
{
    QByteArray text(4096, '\0');
    const ssize_t n
        = ::readlink(QFile::encodeName(source).constData(), text.data(), size_t(text.size()));
    if (n <= 0 || n >= text.size())
        return false;
    text.truncate(int(n));
    const QString written = QFile::decodeName(text);
    const QString from = written.startsWith(QLatin1Char('/'))
        ? written
        : QFileInfo(target).absolutePath() + QLatin1Char('/') + written;
    FileId there;
    return fileIdOf(from, &there) && there == file;
}

MoveOutcome failedWith(MoveOutcome outcome, const QString &message)
{
    outcome.status = MoveOutcome::Status::Failed;
    outcome.message = message;
    return outcome;
}

// O-EXECUTE for one Ready entry. The source and the target are checked again
// first, because the disk may have changed since planning. `fileNow` is given for a link whose
// file this run has moved: the identity of that file where it now is. The link is then there
// though it leads nowhere, and is judged against that file.
MoveOutcome carryOut(const PlannedMove &move, const OrganiseOptions &options,
    const QSet<FileId> &filesOfTheRun, const FileId &ownSource, bool ownKnown,
    const FileId *fileNow = nullptr)
{
    MoveOutcome outcome;
    outcome.status = MoveOutcome::Status::Done;
    outcome.source = move.source;
    outcome.target = move.target;

    if (!QFileInfo::exists(move.source) && !(fileNow && QFileInfo(move.source).isSymLink()))
        return failedWith(outcome, QStringLiteral("the file is gone"));
    const bool targetThere = QFileInfo::exists(move.target);
    if (targetThere && QFileInfo(move.target).isDir())
        return failedWith(outcome, QStringLiteral("a folder is at the target"));
    if (targetThere && !move.replaces)
        return failedWith(outcome, QStringLiteral("something is at the target"));
    // The second lock (O-EXECUTE): by what it is, not by its path, the file
    // at the target is not another file of this run.
    FileId there;
    if (targetThere && !fileIdOf(move.target, &there))
        return failedWith(outcome, QStringLiteral("could not look at the file at the target"));
    if (targetThere && filesOfTheRun.contains(there) && !(ownKnown && there == ownSource)) {
        return failedWith(
            outcome, QStringLiteral("the file at the target is another file of this run"));
    }
    if (!QDir().mkpath(QFileInfo(move.target).absolutePath()))
        return failedWith(outcome, QStringLiteral("could not create the folder"));
    if (fileNow && !linkLeadsTo(move.source, move.target, *fileNow)) {
        return failedWith(
            outcome, QStringLiteral("the link would not lead to its file from there"));
    }

    // The file being replaced waits in a folder of its own, beside the target,
    // until the newcomer is in place. Only then is it trashed or deleted, and
    // the waiting folder goes with it.
    const bool moving = options.mode == OrganiseOptions::Mode::Move;
    QString waiting;
    QString aside;
    if (targetThere) {
        waiting = waitingFolder(move.target);
        if (waiting.isEmpty() || !QDir().mkdir(waiting))
            return failedWith(
                outcome, QStringLiteral("could not put the file being replaced aside"));
        aside = waiting + QLatin1Char('/') + QFileInfo(move.target).fileName();
        if (!QFile::rename(move.target, aside)) {
            QDir().rmdir(waiting);
            return failedWith(
                outcome, QStringLiteral("could not put the file being replaced aside"));
        }
    }

    QString error = organise::transferFile(move.source, move.target, moving);
    if (!error.isEmpty()) {
        if (!aside.isEmpty() && !QFile::rename(aside, move.target))
            error += QStringLiteral("; the old file is at %1").arg(aside);
        else if (!waiting.isEmpty())
            QDir().rmdir(waiting);
        return failedWith(outcome, error);
    }
    outcome.moved = moving; // the file left its source; a copy did not

    if (!aside.isEmpty()) {
        outcome.replaced = true;
        if (options.replaced == OrganiseOptions::Replaced::Delete) {
            if (QFile::remove(aside)) {
                QDir().rmdir(waiting);
            } else {
                outcome.replacedNow = aside;
                outcome.message
                    = QStringLiteral("replaced; the old file is still at %1").arg(aside);
            }
        } else {
            QString inTrash;
            if (sendToTrash(aside, &inTrash)) {
                outcome.replacedNow = inTrash;
                QDir().rmdir(waiting);
                if (!recordTargetPath(inTrash, QFileInfo(waiting).fileName())) {
                    outcome.message = QStringLiteral(
                        "replaced; the trash record does not name the target folder");
                }
            } else {
                outcome.replacedNow = aside;
                outcome.message
                    = QStringLiteral("replaced; the old file is still at %1").arg(aside);
            }
        }
    }
    return outcome;
}

}

// O-EXECUTE. Stops early, with `cancelled` set, when the progress function says so.
OrganiseReport executeOrganise(const QList<PlannedMove> &plan, const OrganiseOptions &options,
    const OrganiseProgress &progress)
{
    OrganiseReport report;
    const int count = static_cast<int>(plan.size());

    // What each entry's source is, noted before anything moves.
    QSet<FileId> filesOfTheRun;
    QList<FileId> sourceIds;
    QList<bool> sourceKnown;
    for (const PlannedMove &entry : plan) {
        FileId id;
        const bool known = fileIdOf(entry.source, &id);
        sourceIds.append(known ? id : FileId());
        sourceKnown.append(known);
        if (known)
            filesOfTheRun.insert(id);
    }
    const bool moving = options.mode == OrganiseOptions::Mode::Move;
    // A link and its own file (Move only): which sources are links, and the entry of each file
    // that is not a link and is Ready.
    QHash<FileId, int> entryOfFile;
    QList<bool> isLink;
    for (int index = 0; index < count; ++index) {
        const PlannedMove &entry = plan.at(index);
        const QFileInfo info(entry.source);
        isLink.append(moving && info.isSymLink());
        if (moving && entry.status == PlannedMove::Status::Ready && sourceKnown.at(index)
            && !isLink.at(index) && !entryOfFile.contains(sourceIds.at(index))) {
            entryOfFile.insert(sourceIds.at(index), index);
        }
    }
    // The entry of the file a link waits for, or -1; and the links waiting for each file.
    QList<int> waitsFor(count, -1);
    QHash<int, QList<int>> waiting;
    for (int index = 0; index < count; ++index) {
        const PlannedMove &entry = plan.at(index);
        if (!isLink.at(index) || entry.status != PlannedMove::Status::Ready
            || !sourceKnown.at(index)) {
            continue;
        }
        const int file = entryOfFile.value(sourceIds.at(index), -1);
        if (file > index) {
            waitsFor[index] = file;
            waiting[file].append(index);
        }
    }

    // The outcomes by entry: a link that waits is filled in after its file, and the report
    // takes them in the plan's order.
    QList<MoveOutcome> outcomes(count);
    QList<bool> have(count, false);
    const auto settle = [&](int index, const MoveOutcome &outcome) {
        if (outcome.status == MoveOutcome::Status::Done)
            ++report.done;
        else if (outcome.status == MoveOutcome::Status::Failed)
            ++report.failed;
        else
            ++report.skipped;
        outcomes[index] = outcome;
        have[index] = true;
    };
    const auto carryOutEntry = [&](int index) {
        const PlannedMove &move = plan.at(index);
        const int file = isLink.at(index) ? entryOfFile.value(sourceIds.at(index), -1) : -1;
        FileId now;
        const bool fileMoved = file >= 0 && have.at(file)
            && outcomes.at(file).status == MoveOutcome::Status::Done && outcomes.at(file).moved
            && fileIdOf(plan.at(file).target, &now);
        settle(index,
            carryOut(move, options, filesOfTheRun, sourceIds.at(index), sourceKnown.at(index),
                fileMoved ? &now : nullptr));
    };
    // The links waiting for the file of `file`, carried out in the plan's order.
    const auto releaseLinksOf = [&](int file) {
        for (const int link : waiting.value(file)) {
            if (!have.at(link))
                carryOutEntry(link);
        }
    };

    int reached = 0;
    for (int index = 0; index < count; ++index) {
        if (progress && !progress(index, count)) {
            report.cancelled = true;
            break;
        }
        reached = index + 1;
        const PlannedMove &move = plan.at(index);
        if (move.status != PlannedMove::Status::Ready) {
            MoveOutcome outcome;
            outcome.status = MoveOutcome::Status::Skipped;
            outcome.source = move.source;
            outcome.target = move.target;
            outcome.message = skipMessage(move.status, move.missingFields);
            settle(index, outcome);
            continue;
        }
        if (waitsFor.at(index) >= 0)
            continue; // its file comes later; the link is carried out straight after it
        carryOutEntry(index);
        releaseLinksOf(index);
    }
    // Stopped while a link waits: it is carried out now, its file being where it is.
    for (int index = 0; index < reached; ++index) {
        if (!have.at(index))
            carryOutEntry(index);
    }
    for (int index = 0; index < reached; ++index)
        report.outcomes.append(outcomes.at(index));

    // O-OTHER, then O-EMPTY: only for moves, and never after a cancelled run.
    if (!report.cancelled && options.mode == OrganiseOptions::Mode::Move) {
        if (options.moveOtherFiles)
            organise::takeAlongOtherFiles(&report);
        if (options.removeEmptyFolders)
            organise::removeEmptiedFolders(&report, options.sourceRoots);
    }
    return report;
}

}
