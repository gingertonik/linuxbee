#pragma once
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

#include "fields/trackfields.h"
#include "paths/pathbuilder.h"
#include "template/template.h"

// Moves or copies music files to where a naming template says they belong.
// Contract: docs/spec/organise.md. Planning never changes the disk; only
// executeOrganise does.
namespace linuxbee {

struct OrganiseOptions {
    // O-TARGET: the folder the template's path is built under. Empty: each
    // file stays in its own folder and only its name changes.
    QString targetRoot;
    // O-TARGET: false keeps each file's current name.
    bool useTemplate = true;
    Template namingTemplate
        = Template::parse(QStringLiteral("<Album Artist>\\<Album>\\<Disc-Track#> <Title>"));
    PathRules
        pathRules; // skipWhenTagsMissing is "do not reorganise files with missing naming tags"
    TrackFieldOptions fieldOptions;

    enum class Mode { Move, Copy };
    Mode mode = Mode::Move;

    // O-CONFLICT: false gives a newcomer the next free name_N.
    bool replaceExisting = false;
    enum class Replaced { MoveToTrash, Delete };
    Replaced replaced = Replaced::MoveToTrash;

    // O-OTHER: Move only. Take the remaining non-music files of a folder along.
    bool moveOtherFiles = false;
    // O-EMPTY: Move only. Remove folders the move left empty.
    bool removeEmptyFolders = true;
    // O-EMPTY: the folders the user named. Nothing at or above one of them is
    // ever removed. Empty: the deepest folder holding every moved track
    // stands in for them.
    QStringList sourceRoots;
};

struct PlannedMove {
    enum class Status {
        Ready, // will be moved or copied to `target`
        AlreadyInPlace, // `target` is where the file is
        MissingTags, // see missingFields
        EmptyName, // the template leaves no file name
        InvalidTemplate,
        NotFound,
        NotAudio,
    };
    Status status = Status::NotFound;
    QString source; // absolute, clean
    QString target; // absolute, clean; Ready and AlreadyInPlace only
    QStringList missingFields;
    bool renamed = false; // O-CONFLICT gave it a numbered name
    bool replaces = false; // an existing file at `target` will be replaced
};

// O-PLAN. One entry per input, in input order. Reads tags; writes nothing.
QList<PlannedMove> planOrganise(const QStringList &files, const OrganiseOptions &options);

struct MoveOutcome {
    enum class Status { Done, Skipped, Failed };
    Status status = Status::Skipped;
    QString source;
    QString target;
    QString message; // Failed: why. Skipped: the plan's status in words.
    bool otherFile = false; // an O-OTHER file, not one of the planned tracks
    // O-REPLACED: a file that was at `target` made way for this one, and
    // where that file is now: its path in the trash, or empty when it was
    // deleted.
    bool replaced = false;
    QString replacedNow;
};

struct OrganiseReport {
    QList<MoveOutcome> outcomes; // the planned moves in order, then O-OTHER files
    QStringList removedFolders; // O-EMPTY, in the order they were removed
    bool cancelled = false;
    int done = 0;
    int skipped = 0;
    int failed = 0;
};

// Called before each planned move with its index and the count. Returning
// false stops the run; what was done stays done.
using OrganiseProgress = std::function<bool(int index, int count)>;

// O-EXECUTE
OrganiseReport executeOrganise(const QList<PlannedMove> &plan, const OrganiseOptions &options,
    const OrganiseProgress &progress = { });

}
