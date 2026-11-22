#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDateTime>
#include <QFile>
#include <QHash>
#include <QTextStream>

#include "command.h"
#include "import/itunesxml.h"
#include "library/library.h"
#include "template/template.h"

namespace {

using linuxbee::cli::usageError;

const char *const kUsage
    = "usage: linuxbeectl library --db FILE count\n"
      "       linuxbeectl library --db FILE scan [--category C] [--now WHEN] FOLDER...\n"
      "       linuxbeectl library --db FILE list [--category C] [--format TEMPLATE]\n"
      "       linuxbeectl library --db FILE import EXPORT [--map OLD=NEW]... [--apply] [--list]";

// L-CLI: a template that does not parse is a usage mistake: "error at N:
// message" on standard error, nothing on standard output, exit 2. Same shape as
// linuxbeectl template already prints.
int printParseError(const linuxbee::TemplateError &error)
{
    QTextStream out(stderr);
    out << QStringLiteral("error at %1: %2").arg(error.position).arg(error.message) << '\n';
    out.flush();
    return 2;
}

// L-CLI: exit 3, with a message on standard error, when the library file cannot
// be opened. A usage mistake is 2 and does something; this is the case where the
// line is fine and the file is not.
int openError(const QString &dbPath, const QString &reason)
{
    QTextStream out(stderr);
    out << QStringLiteral("linuxbeectl: cannot open the library file \"%1\": %2\n")
               .arg(dbPath, reason)
        << '\n';
    out.flush();
    return 3;
}

// L-CLI, L-READFAIL 3: exit 3, nothing on standard output, when the library opened but its tracks
// could not be read.
int readError(const QString &words)
{
    QTextStream err(stderr);
    err << QStringLiteral("linuxbeectl: the library could not be read: %1").arg(words) << '\n';
    err.flush();
    return 3;
}

// L-SCHEMA: one line on standard error when the opening brought the library up to date after a
// copy of it. Standard output is not touched.
void sayStep(const linuxbee::Library &library)
{
    const QString notice = linuxbee::schemaStepNotice(library.stepAtOpening());
    if (notice.isEmpty())
        return;
    QTextStream err(stderr);
    err << QStringLiteral("linuxbeectl: ") << notice << '\n';
    err.flush();
}

// I-CLI: a rating as stars, without a needless ".0".
QString starsText(double stars)
{
    QString text = QString::number(stars, 'f', 1);
    if (text.endsWith(QStringLiteral(".0")))
        text.chop(2);
    return text;
}

// I-CLI: a heading and its entries, two spaces in. The first ten, then how many more, unless all
// were asked for.
void printList(QTextStream &out, const QString &heading, const QStringList &entries, bool all)
{
    if (entries.isEmpty())
        return;
    out << heading << '\n';
    const qsizetype shown = all ? entries.size() : qMin<qsizetype>(entries.size(), 10);
    for (qsizetype i = 0; i < shown; ++i)
        out << QStringLiteral("  ") << entries.at(i) << '\n';
    if (shown < entries.size())
        out << QStringLiteral("  and %1 more (--list shows all)").arg(entries.size() - shown)
            << '\n';
}

// I-CHECK: how the export's answer and the library's compare, each of the two on its own.
struct Check {
    int same = 0;
    int differ = 0;
    int onlyFile = 0;
    int onlyLibrary = 0;
    QString line(const QString &name) const
    {
        return QStringLiteral("%1: %2 the same, %3 differ, %4 only in the file given, %5 only in "
                              "the library")
            .arg(name)
            .arg(same)
            .arg(differ)
            .arg(onlyFile)
            .arg(onlyLibrary);
    }
};

// I-CLI import: `import EXPORT [--map OLD=NEW]... [--apply] [--list]`. The line has been read and
// accepted; the export is read and parsed before the library is opened. Nothing is changed unless
// --apply is given, and only the library's dates and counts ever are: ratings and Love are only
// checked (I-CHECK).
int runImport(const QString &dbPath, const QString &exportPath, const QStringList &maps, bool apply,
    bool listAll)
{
    linuxbee::PathMapping mapping;
    for (const QString &map : maps)
        mapping.append({ map.left(map.indexOf(QLatin1Char('='))),
            map.mid(map.indexOf(QLatin1Char('=')) + 1) });

    QTextStream err(stderr);
    QFile file(exportPath);
    if (!file.open(QIODevice::ReadOnly)) {
        err << QStringLiteral("linuxbeectl: cannot read \"%1\": %2")
                   .arg(exportPath, file.errorString())
            << '\n';
        return 3;
    }
    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        err << QStringLiteral("linuxbeectl: cannot read \"%1\": %2")
                   .arg(exportPath, file.errorString())
            << '\n';
        return 3;
    }
    file.close();
    const linuxbee::ImportedLibrary imported = linuxbee::parseItunesLibrary(bytes);
    if (!imported.ok) {
        err << QStringLiteral(
            "linuxbeectl: \"%1\" is not a library exported by another program: %2")
                   .arg(exportPath, imported.error)
            << '\n';
        return 3;
    }

    QString reason;
    // L-READ: without --apply the import only reads, so the library is opened to read only.
    const std::unique_ptr<linuxbee::Library> library = apply
        ? linuxbee::Library::open(dbPath, &reason)
        : linuxbee::Library::openToRead(dbPath, &reason);
    if (!library)
        return openError(dbPath, reason);
    sayStep(*library);

    // L-READFAIL 3: the library is read before anything is planned or printed.
    const linuxbee::LibraryRead<QList<linuxbee::LibraryTrack>> heldRead = library->readTracks();
    if (heldRead.failed())
        return readError(heldRead.words());
    const QList<linuxbee::LibraryTrack> &held = heldRead.value();
    const linuxbee::ImportPlan plan = linuxbee::planImport(imported, held, mapping);
    QHash<qint64, const linuxbee::LibraryTrack *> byId;
    for (const linuxbee::LibraryTrack &track : held)
        byId.insert(track.id, &track);

    // I-CHECK: compared, reported, never written.
    Check ratings;
    Check love;
    QStringList ratingsDiffer;
    QStringList ratingsOnlyFile;
    QStringList loveDiffers;
    QStringList loveOnlyFile;
    QList<linuxbee::StatisticsMerge> merges;
    int wouldChange = 0;
    for (const linuxbee::ImportMatch &match : plan.matches) {
        if (match.changes) {
            ++wouldChange;
            merges.append({ match.libraryId, match.statistics });
        }
        const linuxbee::LibraryTrack *track = byId.value(match.libraryId);
        if (!track)
            continue;
        bool parsed = false;
        const double heldStars = track->tags.first(QStringLiteral("Rating")).toDouble(&parsed);
        const std::optional<double> inLibrary
            = parsed ? std::optional<double>(heldStars) : std::nullopt;
        if (match.rating && inLibrary) {
            if (*match.rating == *inLibrary) {
                ++ratings.same;
            } else {
                ++ratings.differ;
                ratingsDiffer << QStringLiteral("%1: %2 in the file given, %3 in the library")
                                     .arg(match.libraryPath, starsText(*match.rating),
                                         starsText(*inLibrary));
            }
        } else if (match.rating) {
            ++ratings.onlyFile;
            ratingsOnlyFile << QStringLiteral("%1: %2").arg(
                match.libraryPath, starsText(*match.rating));
        } else if (inLibrary) {
            ++ratings.onlyLibrary;
        }
        const QString loveHeld = track->tags.first(QStringLiteral("Love"));
        if (!match.love.isEmpty() && !loveHeld.isEmpty()) {
            if (match.love == loveHeld) {
                ++love.same;
            } else {
                ++love.differ;
                loveDiffers << QStringLiteral("%1: %2 in the file given, %3 in the library")
                                   .arg(match.libraryPath, match.love, loveHeld);
            }
        } else if (!match.love.isEmpty()) {
            ++love.onlyFile;
            loveOnlyFile << QStringLiteral("%1: %2").arg(match.libraryPath, match.love);
        } else if (!loveHeld.isEmpty()) {
            ++love.onlyLibrary;
        }
    }

    // The one change, before anything is said of it: a refusal prints no result line.
    int changed = 0;
    if (apply) {
        QString words;
        const QMetaObject::Connection listening
            = QObject::connect(library.get(), &linuxbee::Library::failed, library.get(),
                [&words](const QString &what) { words = what; });
        changed = library->mergeStatistics(merges);
        QObject::disconnect(listening);
        if (changed < 0) {
            err << QStringLiteral("linuxbeectl: the import was not stored: %1").arg(words) << '\n';
            return 3;
        }
    }

    QTextStream out(stdout);
    out << QStringLiteral("tracks in the file: %1").arg(imported.tracks.size()) << '\n';
    out << QStringLiteral("in the library: %1").arg(imported.tracks.size() - plan.unmatched.size())
        << '\n';
    out << QStringLiteral("not in the library: %1").arg(plan.unmatched.size()) << '\n';
    out << (apply ? QStringLiteral("changed: %1").arg(changed)
                  : QStringLiteral("would change: %1").arg(wouldChange))
        << '\n';
    out << ratings.line(QStringLiteral("ratings")) << '\n';
    out << love.line(QStringLiteral("love")) << '\n';
    printList(out, QStringLiteral("ratings that differ:"), ratingsDiffer, listAll);
    printList(out, QStringLiteral("ratings only in the file given:"), ratingsOnlyFile, listAll);
    printList(out, QStringLiteral("love that differs:"), loveDiffers, listAll);
    printList(out, QStringLiteral("love only in the file given:"), loveOnlyFile, listAll);
    printList(out, QStringLiteral("not in the library:"), plan.unmatched, listAll);
    out.flush();

    // I-CLI, standard error, the exit status unchanged: a pair that was the first to fit no track
    // of the file, and a file none of whose tracks is here. A pair fits a path when it alone
    // changes it.
    QList<bool> used(mapping.size(), false);
    for (const linuxbee::ImportedTrack &track : imported.tracks) {
        const QString plain = linuxbee::mapImportedPath(track.location, { });
        for (qsizetype k = 0; k < mapping.size(); ++k) {
            if (linuxbee::mapImportedPath(track.location, { mapping.at(k) }) != plain) {
                used[k] = true;
                break;
            }
        }
    }
    for (qsizetype k = 0; k < maps.size(); ++k) {
        if (!used.at(k))
            err << QStringLiteral("linuxbeectl: --map %1 fits no track in the file").arg(maps.at(k))
                << '\n';
    }
    if (!imported.tracks.isEmpty() && imported.tracks.size() == plan.unmatched.size()) {
        const QStringList parts
            = imported.tracks.first().location.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        err << QStringLiteral("linuxbeectl: no track of the file is in the library. The paths in "
                              "the file begin like \"%1\"; --map OLD=NEW says where they are "
                              "here, for example --map Z:=/ for Wine.")
                   .arg(parts.mid(0, 3).join(QLatin1Char('/')))
            << '\n';
    }
    err.flush();
    return 0;
}

int runLibrary(const QStringList &arguments)
{
    QCommandLineParser parser;
    QCommandLineOption dbOption(QStringLiteral("db"),
        QStringLiteral("The library file, created when it is new."), QStringLiteral("FILE"));
    QCommandLineOption categoryOption(QStringLiteral("category"),
        QStringLiteral("The category, as linuxBee names it. Scan: for new files. List: a filter."),
        QStringLiteral("C"));
    QCommandLineOption nowOption(QStringLiteral("now"),
        QStringLiteral("The time of the scan, as an ISO 8601 date and time."),
        QStringLiteral("WHEN"));
    QCommandLineOption formatOption(QStringLiteral("format"),
        QStringLiteral("A naming template, evaluated for every track listed."),
        QStringLiteral("TEMPLATE"));
    QCommandLineOption mapOption(QStringLiteral("map"),
        QStringLiteral("Import: an old folder and a new one, OLD=NEW. May be given several times."),
        QStringLiteral("OLD=NEW"));
    QCommandLineOption applyOption(QStringLiteral("apply"),
        QStringLiteral("Import: store what the file brings. Without it nothing is changed."));
    QCommandLineOption listOption(QStringLiteral("list"),
        QStringLiteral("Import: show every entry of every list, not the first ten."));
    parser.addOptions(
        { dbOption, categoryOption, nowOption, formatOption, mapOption, applyOption, listOption });

    // parse(), not process(): process() exits 1 on an unknown option, and L-CLI
    // requires 2. The parser expects the program name first.
    if (!parser.parse(QStringList { QStringLiteral("linuxbeectl library") } + arguments))
        return usageError(parser.errorText());

    const QString usage = QLatin1String(kUsage);
    const QStringList positional = parser.positionalArguments();
    if (positional.isEmpty())
        return usageError(
            QStringLiteral("linuxbeectl: library needs count, scan or list.\n%1").arg(usage));
    const QString subcommand = positional.at(0);
    if (subcommand != QLatin1String("count") && subcommand != QLatin1String("scan")
        && subcommand != QLatin1String("list") && subcommand != QLatin1String("import"))
        return usageError(QStringLiteral("linuxbeectl: library has no subcommand \"%1\".\n%2")
                .arg(subcommand, usage));
    // L-CLI: every one of these is a usage mistake that does nothing, so the
    // whole line is read and accepted before the library file is touched: no
    // --db, a scan without a folder, an extra argument.
    if (!parser.isSet(dbOption))
        return usageError(QStringLiteral("linuxbeectl: library needs --db FILE.\n%1").arg(usage));
    if (subcommand == QLatin1String("import")) {
        // I-CLI: one export, and every --map has an old folder and an "=".
        if (positional.size() != 2)
            return usageError(
                QStringLiteral("linuxbeectl: library import needs one export file.\n%1")
                    .arg(usage));
        for (const QString &map : parser.values(mapOption)) {
            if (map.indexOf(QLatin1Char('=')) < 1)
                return usageError(QStringLiteral("linuxbeectl: --map needs OLD=NEW, with "
                                                 "something before the \"=\".\n%1")
                        .arg(usage));
        }
        return runImport(parser.value(dbOption), positional.at(1), parser.values(mapOption),
            parser.isSet(applyOption), parser.isSet(listOption));
    }
    if (positional.size() < (subcommand == QLatin1String("scan") ? 2 : 1))
        return usageError(
            QStringLiteral("linuxbeectl: library scan needs a folder.\n%1").arg(usage));
    if (subcommand != QLatin1String("scan") && positional.size() != 1)
        return usageError(QStringLiteral("linuxbeectl: library %1 takes no extra argument.\n%2")
                .arg(subcommand, usage));

    // L-CATEGORY: an unknown name is a usage mistake. L-CLI: for scan an unset
    // --category is Music; for list it asks for every category.
    const QString category
        = parser.isSet(categoryOption) ? parser.value(categoryOption) : QStringLiteral("Music");
    if (parser.isSet(categoryOption) && !linuxbee::Library::categories().contains(category))
        return usageError(
            QStringLiteral("linuxbeectl: \"%1\" is not a category.\n%2").arg(category, usage));

    // L-SCAN: --now becomes the date added of new files, and without it the
    // scan uses the current time. L-CLI: a --now that is not a date is 2.
    QDateTime now = QDateTime::currentDateTimeUtc();
    if (subcommand == QLatin1String("scan") && parser.isSet(nowOption)) {
        now = QDateTime::fromString(parser.value(nowOption), Qt::ISODate).toUTC();
        if (!now.isValid())
            return usageError(
                QStringLiteral("linuxbeectl: --now is not an ISO 8601 date and time.\n%1")
                    .arg(usage));
    }

    // L-CLI: a template that does not parse is 2, doing nothing, so it is
    // checked here and not after the file is opened.
    const bool hasFormat = subcommand == QLatin1String("list") && parser.isSet(formatOption);
    linuxbee::Template format;
    if (hasFormat) {
        format = linuxbee::Template::parse(parser.value(formatOption));
        if (!format.isValid())
            return printParseError(format.error());
    }

    QString reason;
    // L-READ: count and list only read; scan writes.
    const std::unique_ptr<linuxbee::Library> library = subcommand == QLatin1String("scan")
        ? linuxbee::Library::open(parser.value(dbOption), &reason)
        : linuxbee::Library::openToRead(parser.value(dbOption), &reason);
    if (!library)
        return openError(parser.value(dbOption), reason);
    sayStep(*library);

    QTextStream out(stdout);
    if (subcommand == QLatin1String("count")) {
        // L-CLI: the number of tracks, missing ones included.
        const linuxbee::LibraryRead<int> counted = library->readCount();
        if (counted.failed())
            return readError(counted.words());
        out << counted.value() << '\n';
        return 0;
    }
    if (subcommand == QLatin1String("scan")) {
        linuxbee::ScanOptions options;
        options.category = category;
        options.now = now;
        const linuxbee::ScanReport report = library->scan(positional.mid(1), options);
        // L-CLI, L-FAILED: a scan that could not be stored prints no result line, which would
        // say "added 0" for what did not happen; the words go to standard error, exit 3.
        if (!report.refused.isEmpty()) {
            QTextStream err(stderr);
            err << QStringLiteral("linuxbeectl: the scan was not stored: %1").arg(report.refused)
                << '\n';
            err.flush();
            return 3;
        }
        // L-CLI: the one line, in the spec's words and order.
        out << QStringLiteral("added %1, updated %2, unchanged %3, missing %4, found again %5, "
                              "not music %6")
                   .arg(report.added.size())
                   .arg(report.updated.size())
                   .arg(report.unchanged)
                   .arg(report.nowMissing.size())
                   .arg(report.found.size())
                   .arg(report.notAudio.size())
            << '\n';
        // L-CLI, L-FOLLOW: directly after the result line; nothing when none was followed.
        if (!report.followed.isEmpty()) {
            out << (report.followed.size() == 1
                    ? QStringLiteral("followed 1 file that was moved")
                    : QStringLiteral("followed %1 files that were moved")
                          .arg(report.followed.size()))
                << '\n';
        }
        // L-CLI, L-NAMES: the names passed over, after that line, on standard
        // output; the exit status does not change.
        if (report.unreadableNames > 0) {
            out << QStringLiteral("passed over %1 %2 not valid UTF-8, in:")
                       .arg(report.unreadableNames)
                       .arg(report.unreadableNames == 1 ? QStringLiteral("name that is")
                                                        : QStringLiteral("names that are"))
                << '\n';
            for (const QString &folder : report.unreadableIn)
                out << QStringLiteral("  ") << folder << '\n';
        }
        return 0;
    }
    // L-CLI: one line per track, in ascending order of path, which is the order
    // tracks() gives; an empty category asks for every track.
    // L-READFAIL 3: the whole list is read before the first line is printed.
    const linuxbee::LibraryRead<QList<linuxbee::LibraryTrack>> listed
        = library->readTracks(parser.isSet(categoryOption) ? category : QString());
    if (listed.failed())
        return readError(listed.words());
    for (const linuxbee::LibraryTrack &track : listed.value()) {
        if (hasFormat)
            out << format.evaluate(linuxbee::LibraryFieldSource(track)) << '\n';
        else
            out << track.path << '\n';
    }
    return 0;
}

}

LINUXBEE_COMMAND(library, "library", "Scan and list a music library.", runLibrary)
