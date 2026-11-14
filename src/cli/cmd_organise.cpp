#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>

#include "command.h"
#include "files/musicfiles.h"
#include "organise/organiser.h"
#include "template/template.h"

namespace {

using linuxbee::OrganiseOptions;
using linuxbee::PlannedMove;
using linuxbee::cli::usageError;

const char *const kUsage = "usage: linuxbeectl organise [--to DIR] [--template T | --keep-names]\n"
                           "                             [--copy] [--replace] [--delete-replaced]\n"
                           "                             [--allow-missing] [--other-files]\n"
                           "                             [--remove-empty-folders] [--dry-run]\n"
                           "                             FILE_OR_FOLDER...";

// The words for an entry that will be skipped. These match the skip messages
// in src/core/organise/execute.cpp, which the real run prints.
QString skipReason(const PlannedMove &move)
{
    switch (move.status) {
    case PlannedMove::Status::AlreadyInPlace:
        return QStringLiteral("already in place");
    case PlannedMove::Status::MissingTags:
        return QStringLiteral("missing tags: %1")
            .arg(move.missingFields.join(QStringLiteral(", ")));
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

// O-CLI: an invalid template prints "error at N: message" on standard error and exits 2.
int printTemplateError(const linuxbee::TemplateError &error)
{
    QTextStream err(stderr);
    err << QStringLiteral("error at %1: %2").arg(error.position).arg(error.message) << '\n';
    err.flush();
    return 2;
}

// O-CLI --dry-run: what would happen, and nothing is touched.
void printPlanned(QTextStream &out, const PlannedMove &move, OrganiseOptions::Mode mode)
{
    if (move.status != PlannedMove::Status::Ready) {
        out << QStringLiteral("skip: %1: %2\n").arg(move.source, skipReason(move));
        return;
    }
    const QString verb = mode == OrganiseOptions::Mode::Copy ? QStringLiteral("would copy")
                                                             : QStringLiteral("would move");
    out << QStringLiteral("%1: %2 -> %3%4\n")
               .arg(verb, move.source, move.target,
                   move.replaces ? QStringLiteral(" (replaces)") : QString());
}

// O-REPLACED: what became of the file a new one replaced. A file still in the
// waiting folder is named where it was left; one in the trash sits in a `files` folder.
QString replacedWords(const linuxbee::MoveOutcome &outcome)
{
    if (outcome.replacedNow.isEmpty())
        return QStringLiteral("the old file was deleted");
    if (QFileInfo(outcome.replacedNow).dir().dirName() == QLatin1String("files"))
        return QStringLiteral("the old file is in the trash: %1").arg(outcome.replacedNow);
    return QStringLiteral("the old file is still at %1").arg(outcome.replacedNow);
}

// O-CLI: one line per outcome. Skips go to standard output, failures to standard error.
void printOutcome(QTextStream &out, QTextStream &err, const linuxbee::MoveOutcome &outcome,
    OrganiseOptions::Mode mode)
{
    switch (outcome.status) {
    case linuxbee::MoveOutcome::Status::Done: {
        const QString verb = mode == OrganiseOptions::Mode::Copy ? QStringLiteral("copied")
                                                                 : QStringLiteral("moved");
        out << QStringLiteral("%1: %2 -> %3\n").arg(verb, outcome.source, outcome.target);
        if (outcome.replaced)
            out << QStringLiteral("replaced: %1 (%2)\n")
                       .arg(outcome.target, replacedWords(outcome));
        if (!outcome.message.isEmpty())
            err << QStringLiteral("warning: %1: %2\n").arg(outcome.source, outcome.message);
        break;
    }
    case linuxbee::MoveOutcome::Status::Skipped:
        out << QStringLiteral("skip: %1: %2\n").arg(outcome.source, outcome.message);
        break;
    case linuxbee::MoveOutcome::Status::Failed:
        err << QStringLiteral("failed: %1: %2\n").arg(outcome.source, outcome.message);
        break;
    }
}

int runOrganise(const QStringList &arguments)
{
    QCommandLineParser parser;
    const QCommandLineOption toOption(QStringLiteral("to"),
        QStringLiteral("Folder the files are organised under."), QStringLiteral("DIR"));
    const QCommandLineOption templateOption(QStringLiteral("template"),
        QStringLiteral("Naming template, in place of the default."), QStringLiteral("T"));
    const QCommandLineOption keepNamesOption(
        QStringLiteral("keep-names"), QStringLiteral("Keep each file's current name. Needs --to."));
    const QCommandLineOption copyOption(
        QStringLiteral("copy"), QStringLiteral("Copy the files instead of moving them."));
    const QCommandLineOption replaceOption(QStringLiteral("replace"),
        QStringLiteral("Let a new file replace one already at its target."));
    const QCommandLineOption deleteReplacedOption(QStringLiteral("delete-replaced"),
        QStringLiteral("Delete a replaced file instead of moving it to the trash."));
    const QCommandLineOption allowMissingOption(QStringLiteral("allow-missing"),
        QStringLiteral("Organise files whose naming tags are missing, as Unknown."));
    const QCommandLineOption otherFilesOption(QStringLiteral("other-files"),
        QStringLiteral("Also move the other files of a folder the tracks left."));
    const QCommandLineOption removeEmptyOption(QStringLiteral("remove-empty-folders"),
        QStringLiteral("Remove folders that the moves emptied."));
    const QCommandLineOption dryRunOption(
        QStringLiteral("dry-run"), QStringLiteral("Print the plan and change nothing."));
    parser.addOptions({ toOption, templateOption, keepNamesOption, copyOption, replaceOption,
        deleteReplacedOption, allowMissingOption, otherFilesOption, removeEmptyOption,
        dryRunOption });

    // parse(), not process(): an unknown option is a usage mistake, which exits 2.
    if (!parser.parse(QStringList { QStringLiteral("linuxbeectl organise") } + arguments))
        return usageError(QStringLiteral("%1\n%2").arg(parser.errorText(), QLatin1String(kUsage)));

    const QStringList inputs = parser.positionalArguments();
    if (inputs.isEmpty())
        return usageError(QStringLiteral("linuxbeectl: organise needs a file or a folder.\n%1")
                .arg(QLatin1String(kUsage)));
    const bool keepNames = parser.isSet(keepNamesOption);
    if (keepNames && parser.isSet(templateOption))
        return usageError(
            QStringLiteral("linuxbeectl: --template and --keep-names cannot be used together.\n%1")
                .arg(QLatin1String(kUsage)));
    if (keepNames && !parser.isSet(toOption))
        return usageError(
            QStringLiteral("linuxbeectl: --keep-names needs --to.\n%1").arg(QLatin1String(kUsage)));

    OrganiseOptions options;
    options.targetRoot = parser.value(toOption);
    options.useTemplate = !keepNames;
    if (parser.isSet(templateOption)) {
        options.namingTemplate = linuxbee::Template::parse(parser.value(templateOption));
        if (!options.namingTemplate.isValid())
            return printTemplateError(options.namingTemplate.error());
    }
    options.mode
        = parser.isSet(copyOption) ? OrganiseOptions::Mode::Copy : OrganiseOptions::Mode::Move;
    options.replaceExisting = parser.isSet(replaceOption);
    options.replaced = parser.isSet(deleteReplacedOption) ? OrganiseOptions::Replaced::Delete
                                                          : OrganiseOptions::Replaced::MoveToTrash;
    options.pathRules.skipWhenTagsMissing = !parser.isSet(allowMissingOption);
    options.moveOtherFiles = parser.isSet(otherFilesOption);
    options.removeEmptyFolders = parser.isSet(removeEmptyOption);

    // A folder stands for the music files in it, below it too. Each folder
    // named, and the folder of each file named, is a floor O-EMPTY never removes.
    QStringList files;
    for (const QString &input : inputs) {
        if (QFileInfo(input).isDir()) {
            files += linuxbee::findMusicFiles(input, true);
            options.sourceRoots.append(input);
        } else {
            files.append(input);
            options.sourceRoots.append(QFileInfo(input).absolutePath());
        }
    }
    const QList<PlannedMove> plan = linuxbee::planOrganise(files, options);

    QTextStream out(stdout);
    QTextStream err(stderr);
    if (parser.isSet(dryRunOption)) {
        for (const PlannedMove &move : plan)
            printPlanned(out, move, options.mode);
        out.flush();
        return 0;
    }

    const linuxbee::OrganiseReport report = linuxbee::executeOrganise(plan, options);
    for (const linuxbee::MoveOutcome &outcome : report.outcomes)
        printOutcome(out, err, outcome, options.mode);
    for (const QString &folder : report.removedFolders)
        out << QStringLiteral("removed folder: %1\n").arg(folder);
    out.flush();
    err.flush();
    // O-CLI: 3 when anything failed. Skips are not failures.
    return report.failed > 0 ? 3 : 0;
}

}

LINUXBEE_COMMAND(
    organise, "organise", "Move or copy music files to where a naming template says.", runOrganise)
