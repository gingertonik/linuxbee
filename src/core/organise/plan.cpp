#include "organiser.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>

#include "tags/tagreader.h"

namespace linuxbee {

namespace {

// What the earlier entries of a plan have settled (O-CONFLICT): the targets
// already given out, the sources that an earlier Move entry takes away, and
// the sources of every entry of the plan (the files of the run).
struct Settled {
    QSet<QString> targets;
    QSet<QString> leaving;
    QSet<QString> ofTheRun;
};

QString absoluteClean(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

// O-TARGET: the text after the last dot of the file name, case kept. A dot
// that is the first character does not start an extension.
QString extensionOf(const QString &fileName)
{
    const qsizetype dot = fileName.lastIndexOf(QLatin1Char('.'));
    return dot > 0 ? fileName.mid(dot + 1) : QString();
}

QString lastComponent(const QString &relativePath)
{
    return relativePath.mid(relativePath.lastIndexOf(QLatin1Char('/')) + 1);
}

// O-PLAN steps 1 to 4 for one file: everything that does not depend on the
// other entries. A Ready entry still has its conflicts to settle.
PlannedMove planFile(const QString &file, const OrganiseOptions &options)
{
    PlannedMove move;
    move.source = absoluteClean(file);

    // Step 1.
    if (options.useTemplate && !options.namingTemplate.isValid()) {
        move.status = PlannedMove::Status::InvalidTemplate;
        return move;
    }

    // Step 2. Pictures are not needed to name a file.
    ReadOptions readOptions;
    readOptions.pictures = false;
    const ReadResult read = readTags(move.source, readOptions);
    if (read.status == ReadResult::Status::NotFound) {
        move.status = PlannedMove::Status::NotFound;
        return move;
    }
    if (read.status == ReadResult::Status::NotAudio) {
        move.status = PlannedMove::Status::NotAudio;
        return move;
    }

    // Step 3: O-TARGET.
    const QFileInfo info(move.source);
    QString relative;
    if (options.useTemplate) {
        const TrackFieldSource fields(read.tags, move.source, options.fieldOptions);
        const BuiltPath built = buildRelativePath(
            options.namingTemplate, fields, extensionOf(info.fileName()), options.pathRules);
        switch (built.status) {
        case BuiltPath::Status::Ok:
            relative = built.relativePath;
            break;
        case BuiltPath::Status::MissingTags:
            move.status = PlannedMove::Status::MissingTags;
            move.missingFields = built.missingFields;
            return move;
        case BuiltPath::Status::EmptyName:
            move.status = PlannedMove::Status::EmptyName;
            return move;
        case BuiltPath::Status::InvalidTemplate:
            move.status = PlannedMove::Status::InvalidTemplate;
            return move;
        }
    } else {
        relative = info.fileName();
    }

    if (options.targetRoot.isEmpty()) {
        // The file stays in its folder and takes the new name.
        move.target
            = QDir::cleanPath(info.absolutePath() + QLatin1Char('/') + lastComponent(relative));
    } else {
        move.target = QDir::cleanPath(
            QFileInfo(options.targetRoot).absoluteFilePath() + QLatin1Char('/') + relative);
    }

    // Step 4.
    move.status = move.target == move.source ? PlannedMove::Status::AlreadyInPlace
                                             : PlannedMove::Status::Ready;
    return move;
}

// O-CONFLICT for a Ready entry. The file's own current path counts as free.
void settleConflict(PlannedMove *move, const OrganiseOptions &options, const Settled &settled)
{
    const QString own = move->source;
    const auto taken = [&own, &settled](const QString &candidate) {
        if (candidate == own)
            return false;
        if (settled.targets.contains(candidate))
            return true;
        return QFileInfo::exists(candidate) && !settled.leaving.contains(candidate);
    };

    const QString wanted = move->target;
    if (!taken(wanted))
        return;

    // Taken on disk only, and the user allowed replacing it. A target given
    // out by this run is never replaced, nor is a file of the run that has
    // not left: two files of one run never replace each other.
    if (options.replaceExisting && !settled.targets.contains(wanted)
        && !settled.ofTheRun.contains(wanted)) {
        move->replaces = true;
        return;
    }

    const QString numbered = uniqueRelativePath(wanted, taken, options.pathRules);
    if (numbered.isEmpty()) {
        // All 9999 numbered names are taken. The spec does not say what
        // happens here; the target stays as it was and executeOrganise
        // refuses to write over a file it was not told to replace.
        return;
    }
    move->target = numbered;
    move->renamed = numbered != wanted;
    if (numbered == own)
        move->status = PlannedMove::Status::AlreadyInPlace;
}

}

// O-PLAN. One entry per input, in input order.
QList<PlannedMove> planOrganise(const QStringList &files, const OrganiseOptions &options)
{
    QList<PlannedMove> plan;
    Settled settled;
    // Every file is planned before any conflict is settled, so that the
    // sources of the whole run are known.
    for (const QString &file : files) {
        plan.append(planFile(file, options));
        settled.ofTheRun.insert(plan.last().source);
    }
    for (PlannedMove &move : plan) {
        if (move.status == PlannedMove::Status::Ready)
            settleConflict(&move, options, settled);
        if (move.status == PlannedMove::Status::Ready
            || move.status == PlannedMove::Status::AlreadyInPlace) {
            settled.targets.insert(move.target);
        }
        if (move.status == PlannedMove::Status::Ready
            && options.mode == OrganiseOptions::Mode::Move) {
            settled.leaving.insert(move.source);
        }
    }
    return plan;
}

}
