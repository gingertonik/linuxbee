#include "aftermath.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>

#include <algorithm>

#include "files/musicfiles.h"
#include "transfer.h"

namespace linuxbee::organise {

namespace {

// A planned track that was moved: its outcome is Done and it is not an other file.
bool isMovedTrack(const MoveOutcome &outcome)
{
    return !outcome.otherFile && outcome.status == MoveOutcome::Status::Done;
}

QString folderOf(const QString &path)
{
    return QFileInfo(path).absolutePath();
}

// True when a visible music file is directly in `folder`. A hidden file is
// not a track (findMusicFiles skips hidden files), so it does not count.
bool hasMusicFileIn(const QString &folder)
{
    const QStringList names = QDir(folder).entryList(QDir::Files);
    for (const QString &name : names) {
        if (hasMusicExtension(name))
            return true;
    }
    return false;
}

// True for a folder that exists and holds nothing, hidden entries included.
bool isEmptyFolder(const QString &folder)
{
    return QFileInfo(folder).isDir()
        && QDir(folder).isEmpty(
            QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
}

// A folder as an absolute path with no `.`, `..` or trailing slash.
QString tidyFolder(const QString &folder)
{
    return QDir::cleanPath(QDir(folder).absolutePath());
}

// True when `folder` lies strictly below `floor`. Compared part by part, so
// `/music/inbox2` is not below `/music/inbox`.
bool isStrictlyBelow(const QString &folder, const QString &floor)
{
    if (folder == floor)
        return false;
    const QString prefix = floor == QStringLiteral("/") ? floor : floor + QLatin1Char('/');
    return folder.startsWith(prefix);
}

// The deepest folder that holds every folder in `folders`, compared part by part.
QString deepestCommonFolder(const QStringList &folders)
{
    QStringList common;
    bool first = true;
    for (const QString &folder : folders) {
        const QStringList parts = folder.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (first) {
            common = parts;
            first = false;
            continue;
        }
        int same = 0;
        while (same < common.size() && same < parts.size() && common.at(same) == parts.at(same))
            ++same;
        common.resize(same);
    }
    return QLatin1Char('/') + common.join(QStringLiteral("/"));
}

}

void takeAlongOtherFiles(OrganiseReport *report)
{
    // The folders the planned tracks left, in the order they first appear,
    // and for each one the folders its tracks went to.
    QStringList folders;
    QHash<QString, QStringList> destinations;
    for (const MoveOutcome &outcome : report->outcomes) {
        if (!isMovedTrack(outcome))
            continue;
        const QString from = folderOf(outcome.source);
        if (!destinations.contains(from))
            folders.append(from);
        destinations[from].append(folderOf(outcome.target));
    }

    for (const QString &folder : folders) {
        QStringList targets = destinations.value(folder);
        targets.removeDuplicates();
        // O-OTHER: left alone when the tracks went to more than one folder,
        // when they went back to this folder, or when a music file is still here.
        if (targets.size() != 1 || targets.first() == folder || hasMusicFileIn(folder))
            continue;
        const QString destination = targets.first();

        // Every remaining file directly in the folder, in ascending order of name.
        QStringList names = QDir(folder).entryList(QDir::Files | QDir::Hidden);
        names.sort();
        for (const QString &name : names) {
            MoveOutcome outcome;
            outcome.otherFile = true;
            outcome.source = QDir(folder).filePath(name);
            outcome.target = QDir(destination).filePath(name);
            if (QFileInfo::exists(outcome.target)) {
                outcome.status = MoveOutcome::Status::Skipped;
                outcome.message = QStringLiteral("a file of that name is already there");
                ++report->skipped;
            } else {
                const QString error = transferFile(outcome.source, outcome.target, true);
                if (error.isEmpty()) {
                    outcome.status = MoveOutcome::Status::Done;
                    outcome.moved = true;
                    ++report->done;
                } else {
                    outcome.status = MoveOutcome::Status::Failed;
                    outcome.message = error;
                    ++report->failed;
                }
            }
            report->outcomes.append(outcome);
        }
    }
}

void removeEmptiedFolders(OrganiseReport *report, const QStringList &sourceRoots)
{
    // The folders the planned tracks left, in the order they first appear.
    QStringList folders;
    for (const MoveOutcome &outcome : report->outcomes) {
        if (!isMovedTrack(outcome))
            continue;
        const QString from = folderOf(outcome.source);
        if (!folders.contains(from))
            folders.append(from);
    }

    // O-EMPTY floors: the folders the user named, or with none given, the
    // deepest folder holding every moved track.
    QStringList floors;
    for (const QString &root : sourceRoots) {
        if (!root.isEmpty())
            floors.append(tidyFolder(root));
    }
    if (floors.isEmpty() && !folders.isEmpty())
        floors.append(deepestCommonFolder(folders));

    // O-EMPTY: each folder, then its parent, and so on up, while each lies below
    // a floor, is not a floor itself, and is empty. A floor, and anything above
    // it, stays, whatever other floor lies above it.
    for (const QString &start : folders) {
        QString folder = start;
        while (!floors.contains(folder)
            && std::any_of(floors.cbegin(), floors.cend(),
                [&folder](const QString &floor) { return isStrictlyBelow(folder, floor); })
            && isEmptyFolder(folder)) {
            const QString parent = QFileInfo(folder).absolutePath();
            if (parent == folder || !QDir().rmdir(folder))
                break;
            report->removedFolders.append(folder);
            folder = parent;
        }
    }
}

}
