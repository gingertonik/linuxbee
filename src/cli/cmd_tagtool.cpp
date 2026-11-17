#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QTextStream>

#include <functional>

#include "command.h"
#include "tagtools/common.h"
#include "tagtools/tagtools.h"
#include "tags/tagreader.h"
#include "tags/tagwriter.h"

namespace {

using linuxbee::CaseOptions;
using linuxbee::CaseStyle;
using linuxbee::RemoveOptions;
using linuxbee::RenumberOptions;
using linuxbee::SearchReplaceOptions;
using linuxbee::TagChanges;
using linuxbee::TrackTags;
using linuxbee::cli::usageError;
using linuxbee::tagtools::canonicalField;

const char *const kUsage
    = "usage: linuxbeectl tagtool replace --find TEXT [--replace TEXT] [--field NAME]... "
      "[--regex] [--match-case] [--whole-word] [--dry-run] FILE...\n"
      "       linuxbeectl tagtool case [--style each-word|title|sentence|upper|lower] "
      "[--field NAME]... [--dry-run] FILE...\n"
      "       linuxbeectl tagtool remove [--field NAME]... [--all] [--pictures] [--dry-run] "
      "FILE...\n"
      "       linuxbeectl tagtool renumber [--start N] [--digits N] [--total] [--dry-run] FILE...\n"
      "       linuxbeectl tagtool infer --pattern PATTERN [--overwrite] [--dry-run] FILE...\n"
      "       linuxbeectl tagtool copy --from FIELD --to FIELD [--dry-run] FILE...\n"
      "       linuxbeectl tagtool swap --from FIELD --to FIELD [--dry-run] FILE...";

const QStringList kTools { QStringLiteral("replace"), QStringLiteral("case"),
    QStringLiteral("remove"), QStringLiteral("renumber"), QStringLiteral("infer"),
    QStringLiteral("copy"), QStringLiteral("swap") };

QCommandLineOption flagOption(const char *name, const char *description)
{
    return QCommandLineOption(QString::fromLatin1(name), QString::fromLatin1(description));
}

QCommandLineOption withValue(const char *name, const char *description, const char *valueName)
{
    return QCommandLineOption(QString::fromLatin1(name), QString::fromLatin1(description),
        QString::fromLatin1(valueName));
}

// The options each tool takes. Any other option is a usage mistake.
QList<QCommandLineOption> optionsFor(const QString &tool)
{
    QList<QCommandLineOption> options { flagOption(
        "dry-run", "Print the changes and write nothing.") };
    if (tool == QLatin1String("replace")) {
        options << withValue("find", "Text to find.", "TEXT")
                << withValue("replace",
                       "Text to put in its place. With --regex, $1 to $9 are groups.", "TEXT")
                << withValue("field",
                       "Field to search. Repeat for several. Default: every field but Rating.",
                       "NAME")
                << flagOption("regex", "Treat --find as a regular expression.")
                << flagOption("match-case", "Match letter case.")
                << flagOption("whole-word", "Match only whole words.");
    } else if (tool == QLatin1String("case")) {
        options << withValue(
            "style", "each-word (default), title, sentence, upper or lower.", "STYLE")
                << withValue("field",
                       "Field to change. Repeat for several. Default: Title, Artist, Album, Album "
                       "Artist.",
                       "NAME");
    } else if (tool == QLatin1String("remove")) {
        options << withValue(
            "field", "Field, or other tag key, to remove. Repeat for several.", "NAME")
                << flagOption("all", "Remove every field and other tag.")
                << flagOption("pictures", "Remove every picture.");
    } else if (tool == QLatin1String("renumber")) {
        options << withValue("start", "Number of the first track. Default: 1.", "N")
                << withValue("digits", "Pad track numbers with zeros to this many digits.", "N")
                << flagOption("total", "Also set Track Count to the number of files.");
    } else if (tool == QLatin1String("infer")) {
        options << withValue("pattern", "Pattern such as <Artist>/<Track#> - <Title>.", "PATTERN")
                << flagOption("overwrite", "Replace values the file already has.");
    } else {
        options << withValue("from", "Field to copy or swap from.", "FIELD")
                << withValue("to", "Field to copy or swap to.", "FIELD");
    }
    return options;
}

// Works each file on its own. `paths` and `tags` line up.
using Planner
    = std::function<QList<TagChanges>(const QStringList &paths, const QList<TrackTags> &tags)>;

Planner perFile(std::function<TagChanges(const QString &path, const TrackTags &tags)> one)
{
    return [one](const QStringList &paths, const QList<TrackTags> &tags) {
        QList<TagChanges> results;
        for (qsizetype i = 0; i < tags.size(); ++i)
            results.append(one(paths.at(i), tags.at(i)));
        return results;
    };
}

// NUMBER must be a whole number of 0 or more.
bool wholeNumber(const QString &text, int *number)
{
    bool ok = false;
    const int value = text.toInt(&ok);
    if (!ok || value < 0)
        return false;
    *number = value;
    return true;
}

// G-CLI, infer: where the file is. A relative FILE is made absolute from the
// working directory, and `.` and `..` are worked out as text. Links are not
// followed, so a link is matched by its own name and folder.
QString wherePath(const QString &file)
{
    return QDir::cleanPath(QDir::current().absoluteFilePath(file));
}

// G-CLI: the changes of one file, fields in code-point order of their names,
// then other keys, then pictures.
void printChanges(
    QTextStream &out, const QString &path, const TrackTags &tags, const TagChanges &changes)
{
    for (auto it = changes.fields.cbegin(); it != changes.fields.cend(); ++it) {
        out << path << ": " << it.key() << ": \""
            << tags.fields.value(it.key()).join(QLatin1String("; ")) << "\" -> \""
            << it.value().join(QLatin1String("; ")) << "\"\n";
    }
    for (auto it = changes.other.cbegin(); it != changes.other.cend(); ++it) {
        out << path << ": other " << it.key() << ": \""
            << tags.other.value(it.key()).join(QLatin1String("; ")) << "\" -> \""
            << it.value().join(QLatin1String("; ")) << "\"\n";
    }
    if (changes.pictures.has_value()) {
        out << path << ": pictures: " << tags.pictures.size() << " -> " << changes.pictures->size()
            << '\n';
    }
}

QString reasonFor(const linuxbee::WriteResult &result)
{
    switch (result.status) {
    case linuxbee::WriteResult::Status::Ok:
    case linuxbee::WriteResult::Status::Unchanged:
        return { };
    case linuxbee::WriteResult::Status::NotFound:
        return QStringLiteral("not found");
    case linuxbee::WriteResult::Status::NotAudio:
        return QStringLiteral("not a music file");
    case linuxbee::WriteResult::Status::NotWritable:
        return QStringLiteral("not writable");
    case linuxbee::WriteResult::Status::InvalidChange:
        return result.problems.join(QLatin1String("; "));
    case linuxbee::WriteResult::Status::HardLinked:
    case linuxbee::WriteResult::Status::ChangedOnDisk:
        // WS-LINK, WS-SAME: the line the writer gave.
        return result.problems.isEmpty() ? QStringLiteral("could not save")
                                         : result.problems.first();
    case linuxbee::WriteResult::Status::Failed:
        return QStringLiteral("could not save");
    }
    return QStringLiteral("could not save");
}

// G-CLI. Usage is checked first (exit 2, nothing read); every file is read
// before anything is printed or written (exit 3 if one fails); then the
// changes are printed and, unless --dry-run, saved.
int runTagtool(const QStringList &arguments)
{
    if (arguments.isEmpty() || !kTools.contains(arguments.first()))
        return usageError(
            QStringLiteral("linuxbeectl: tagtool needs one of replace, case, remove, renumber, "
                           "infer, copy, swap.\n%1")
                .arg(QLatin1String(kUsage)));
    const QString tool = arguments.first();

    QCommandLineParser parser;
    parser.addOptions(optionsFor(tool));
    // parse(), not process(): a usage mistake exits 2, as G-CLI wants.
    if (!parser.parse(QStringList { QStringLiteral("linuxbeectl tagtool") } + arguments))
        return usageError(parser.errorText());

    const QStringList files = parser.positionalArguments().mid(1);
    if (files.isEmpty())
        return usageError(QStringLiteral("linuxbeectl: tagtool %1 needs at least one FILE.\n%2")
                .arg(tool, QLatin1String(kUsage)));

    const QString noField = QStringLiteral("linuxbeectl: no field named \"%1\".");
    Planner planner;
    if (tool == QLatin1String("replace")) {
        if (!parser.isSet(QStringLiteral("find")))
            return usageError(QStringLiteral("linuxbeectl: replace needs --find.\n%1")
                    .arg(QLatin1String(kUsage)));
        SearchReplaceOptions options;
        options.find = parser.value(QStringLiteral("find"));
        options.replace = parser.value(QStringLiteral("replace"));
        options.fields = parser.values(QStringLiteral("field"));
        options.regex = parser.isSet(QStringLiteral("regex"));
        options.matchCase = parser.isSet(QStringLiteral("match-case"));
        options.wholeWord = parser.isSet(QStringLiteral("whole-word"));
        const QString problem = linuxbee::validateSearchReplace(options);
        if (!problem.isEmpty())
            return usageError(QStringLiteral("linuxbeectl: %1").arg(problem));
        planner = perFile([options](const QString &, const TrackTags &tags) {
            return linuxbee::searchReplace(tags, options);
        });
    } else if (tool == QLatin1String("case")) {
        const QString style = parser.isSet(QStringLiteral("style"))
            ? parser.value(QStringLiteral("style"))
            : QStringLiteral("each-word");
        CaseOptions options;
        if (style == QLatin1String("each-word"))
            options.style = CaseStyle::EachWord;
        else if (style == QLatin1String("title"))
            options.style = CaseStyle::TitleCase;
        else if (style == QLatin1String("sentence"))
            options.style = CaseStyle::Sentence;
        else if (style == QLatin1String("upper"))
            options.style = CaseStyle::Upper;
        else if (style == QLatin1String("lower"))
            options.style = CaseStyle::Lower;
        else
            return usageError(QStringLiteral("linuxbeectl: no style named \"%1\".\n%2")
                    .arg(style, QLatin1String(kUsage)));
        for (const QString &name : parser.values(QStringLiteral("field"))) {
            const QString field = canonicalField(name);
            if (field.isEmpty())
                return usageError(noField.arg(name));
            options.fields.append(field);
        }
        planner = perFile([options](const QString &, const TrackTags &tags) {
            return linuxbee::capitalise(tags, options);
        });
    } else if (tool == QLatin1String("remove")) {
        RemoveOptions options;
        options.fields = parser.values(QStringLiteral("field"));
        options.allTags = parser.isSet(QStringLiteral("all"));
        options.removePictures = parser.isSet(QStringLiteral("pictures"));
        if (options.fields.isEmpty() && !options.allTags && !options.removePictures)
            return usageError(
                QStringLiteral("linuxbeectl: remove needs --field, --all or --pictures.\n%1")
                    .arg(QLatin1String(kUsage)));
        planner = perFile([options](const QString &, const TrackTags &tags) {
            return linuxbee::removeTags(tags, options);
        });
    } else if (tool == QLatin1String("renumber")) {
        RenumberOptions options;
        if (parser.isSet(QStringLiteral("start"))
            && !wholeNumber(parser.value(QStringLiteral("start")), &options.start))
            return usageError(
                QStringLiteral("linuxbeectl: --start needs a whole number of 0 or more.\n%1")
                    .arg(QLatin1String(kUsage)));
        if (parser.isSet(QStringLiteral("digits"))) {
            if (!wholeNumber(parser.value(QStringLiteral("digits")), &options.digits))
                return usageError(
                    QStringLiteral("linuxbeectl: --digits needs a whole number of 0 or more.\n%1")
                        .arg(QLatin1String(kUsage)));
            if (options.digits > 9)
                return usageError(QStringLiteral("linuxbeectl: --digits takes nine at most.\n%1")
                        .arg(QLatin1String(kUsage)));
        }
        options.writeTotal = parser.isSet(QStringLiteral("total"));
        planner = [options](const QStringList &, const QList<TrackTags> &tags) {
            return linuxbee::renumberTracks(tags, options);
        };
    } else if (tool == QLatin1String("infer")) {
        if (!parser.isSet(QStringLiteral("pattern")))
            return usageError(QStringLiteral("linuxbeectl: infer needs --pattern.\n%1")
                    .arg(QLatin1String(kUsage)));
        const QString pattern = parser.value(QStringLiteral("pattern"));
        const QString problem = linuxbee::validateInferPattern(pattern);
        if (!problem.isEmpty())
            return usageError(QStringLiteral("linuxbeectl: %1").arg(problem));
        const bool overwrite = parser.isSet(QStringLiteral("overwrite"));
        planner = perFile([pattern, overwrite](const QString &path, const TrackTags &tags) {
            return linuxbee::inferTags(tags, wherePath(path), pattern, !overwrite);
        });
    } else {
        if (!parser.isSet(QStringLiteral("from")) || !parser.isSet(QStringLiteral("to")))
            return usageError(QStringLiteral("linuxbeectl: %1 needs --from and --to.\n%2")
                    .arg(tool, QLatin1String(kUsage)));
        const QString from = canonicalField(parser.value(QStringLiteral("from")));
        if (from.isEmpty())
            return usageError(noField.arg(parser.value(QStringLiteral("from"))));
        const QString to = canonicalField(parser.value(QStringLiteral("to")));
        if (to.isEmpty())
            return usageError(noField.arg(parser.value(QStringLiteral("to"))));
        const bool copy = tool == QLatin1String("copy");
        planner = perFile([from, to, copy](const QString &, const TrackTags &tags) {
            return copy ? linuxbee::copyTag(tags, from, to) : linuxbee::swapTags(tags, from, to);
        });
    }

    QTextStream err(stderr);
    QList<TrackTags> tags;
    int code = 0;
    for (const QString &path : files) {
        linuxbee::ReadOptions readOptions;
        readOptions.pictureData = false;
        const linuxbee::ReadResult result = linuxbee::readTags(path, readOptions);
        if (result.status == linuxbee::ReadResult::Status::Ok) {
            tags.append(result.tags);
            continue;
        }
        err << "failed: " << path << ": "
            << (result.status == linuxbee::ReadResult::Status::NotFound ? "not found"
                                                                        : "not a music file")
            << '\n';
        code = 3;
    }
    if (code != 0) {
        err.flush();
        return code;
    }

    const QList<TagChanges> changes = planner(files, tags);
    QTextStream out(stdout);
    for (qsizetype i = 0; i < files.size(); ++i)
        printChanges(out, files.at(i), tags.at(i), changes.at(i));
    out.flush();
    if (parser.isSet(QStringLiteral("dry-run")))
        return 0;

    for (qsizetype i = 0; i < files.size(); ++i) {
        if (changes.at(i).isEmpty())
            continue;
        const linuxbee::WriteResult written = linuxbee::writeTags(files.at(i), changes.at(i));
        if (!written.ok()) {
            err << "failed: " << files.at(i) << ": " << reasonFor(written) << '\n';
            code = 3;
        }
    }
    err.flush();
    return code;
}

}

LINUXBEE_COMMAND(tagtool, "tagtool",
    "Bulk tag tools: replace, case, remove, renumber, infer, copy, swap.", runTagtool)
