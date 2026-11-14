#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDateTime>
#include <QTextStream>

#include "command.h"
#include "library/library.h"
#include "template/template.h"

namespace {

using linuxbee::cli::usageError;

const char *const kUsage
    = "usage: linuxbeectl library --db FILE count\n"
      "       linuxbeectl library --db FILE scan [--category C] [--now WHEN] FOLDER...\n"
      "       linuxbeectl library --db FILE list [--category C] [--format TEMPLATE]";

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
    parser.addOptions({ dbOption, categoryOption, nowOption, formatOption });

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
        && subcommand != QLatin1String("list"))
        return usageError(QStringLiteral("linuxbeectl: library has no subcommand \"%1\".\n%2")
                .arg(subcommand, usage));
    // L-CLI: every one of these is a usage mistake that does nothing, so the
    // whole line is read and accepted before the library file is touched: no
    // --db, a scan without a folder, an extra argument.
    if (!parser.isSet(dbOption))
        return usageError(QStringLiteral("linuxbeectl: library needs --db FILE.\n%1").arg(usage));
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
    const std::unique_ptr<linuxbee::Library> library
        = linuxbee::Library::open(parser.value(dbOption), &reason);
    if (!library)
        return openError(parser.value(dbOption), reason);

    QTextStream out(stdout);
    if (subcommand == QLatin1String("count")) {
        // L-CLI: the number of tracks, missing ones included.
        out << library->count() << '\n';
        return 0;
    }
    if (subcommand == QLatin1String("scan")) {
        linuxbee::ScanOptions options;
        options.category = category;
        options.now = now;
        const linuxbee::ScanReport report = library->scan(positional.mid(1), options);
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
    for (const linuxbee::LibraryTrack &track :
        library->tracks(parser.isSet(categoryOption) ? category : QString())) {
        if (hasFormat)
            out << format.evaluate(linuxbee::LibraryFieldSource(track)) << '\n';
        else
            out << track.path << '\n';
    }
    return 0;
}

}

LINUXBEE_COMMAND(library, "library", "Scan and list a music library.", runLibrary)
