#include "organiser.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

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

// A name in the target's folder that no file has yet, for the file being replaced.
QString asideName(const QString &target)
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

// Sends a file to the desktop's trash. QFile::moveToTrash fails while the trash
// folders are missing, as on a fresh account, so they are made first, the way
// a file manager makes them.
bool sendToTrash(const QString &path)
{
    const QString dataHome = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (dataHome.isEmpty())
        return false;
    const QString trash = dataHome + QStringLiteral("/Trash");
    if (!QDir().mkpath(trash + QStringLiteral("/files"))
        || !QDir().mkpath(trash + QStringLiteral("/info"))) {
        return false;
    }
    return QFile::moveToTrash(path);
}

MoveOutcome failedWith(MoveOutcome outcome, const QString &message)
{
    outcome.status = MoveOutcome::Status::Failed;
    outcome.message = message;
    return outcome;
}

// O-EXECUTE for one Ready entry. The source and the target are checked again
// first, because the disk may have changed since planning.
MoveOutcome carryOut(const PlannedMove &move, const OrganiseOptions &options)
{
    MoveOutcome outcome;
    outcome.status = MoveOutcome::Status::Done;
    outcome.source = move.source;
    outcome.target = move.target;

    if (!QFileInfo::exists(move.source))
        return failedWith(outcome, QStringLiteral("the file is gone"));
    const bool targetThere = QFileInfo::exists(move.target);
    if (targetThere && QFileInfo(move.target).isDir())
        return failedWith(outcome, QStringLiteral("a folder is at the target"));
    if (targetThere && !move.replaces)
        return failedWith(outcome, QStringLiteral("something is at the target"));
    if (!QDir().mkpath(QFileInfo(move.target).absolutePath()))
        return failedWith(outcome, QStringLiteral("could not create the folder"));

    // The file being replaced is put aside, and dealt with only once the
    // newcomer is in place.
    const bool moving = options.mode == OrganiseOptions::Mode::Move;
    QString aside;
    if (targetThere) {
        aside = asideName(move.target);
        if (aside.isEmpty() || !QFile::rename(move.target, aside))
            return failedWith(
                outcome, QStringLiteral("could not put the file being replaced aside"));
    }

    QString error = organise::transferFile(move.source, move.target, moving);
    if (!error.isEmpty()) {
        if (!aside.isEmpty() && !QFile::rename(aside, move.target))
            error += QStringLiteral("; the old file is at %1").arg(aside);
        return failedWith(outcome, error);
    }

    if (!aside.isEmpty()) {
        const bool disposed = options.replaced == OrganiseOptions::Replaced::Delete
            ? QFile::remove(aside)
            : sendToTrash(aside);
        if (!disposed)
            outcome.message = QStringLiteral("replaced; the old file is still at %1").arg(aside);
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
    for (int index = 0; index < count; ++index) {
        if (progress && !progress(index, count)) {
            report.cancelled = true;
            break;
        }
        const PlannedMove &move = plan.at(index);
        if (move.status != PlannedMove::Status::Ready) {
            MoveOutcome outcome;
            outcome.status = MoveOutcome::Status::Skipped;
            outcome.source = move.source;
            outcome.target = move.target;
            outcome.message = skipMessage(move.status, move.missingFields);
            ++report.skipped;
            report.outcomes.append(outcome);
            continue;
        }
        const MoveOutcome outcome = carryOut(move, options);
        if (outcome.status == MoveOutcome::Status::Done)
            ++report.done;
        else
            ++report.failed;
        report.outcomes.append(outcome);
    }
    return report;
}

}
