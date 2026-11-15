#include <QCommandLineParser>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QTextStream>

#include "command.h"
#include "covers/cover.h"

namespace {

using linuxbee::Cover;
using linuxbee::CoverOptions;
using linuxbee::cli::usageError;

const char *const kUsage = "usage: linuxbeectl cover show FILE [--file-first]\n"
                           "       linuxbeectl cover extract FILE OUT [--file-first]";

// CV-CLI: exit 3 when there is no cover or OUT cannot be written.
constexpr int kFailed = 3;

int reportFailure(const QString &reason)
{
    QTextStream err(stderr);
    err << QStringLiteral("failed: %1").arg(reason) << '\n';
    err.flush();
    return kFailed;
}

// CV-CLI: --file-first puts the folder's file before the embedded picture.
CoverOptions optionsFor(bool fileFirst, bool wantData)
{
    CoverOptions options;
    options.data = wantData;
    if (fileFirst)
        options.order = { Cover::Source::File, Cover::Source::Embedded };
    return options;
}

// CV-CLI: what findCover answers for FILE, one name=value line each.
int showCover(const QString &file, bool fileFirst)
{
    const Cover cover = linuxbee::findCover(file, optionsFor(fileFirst, false));
    QTextStream out(stdout);
    if (cover.source == Cover::Source::None) {
        out << QStringLiteral("source=none") << '\n';
    } else {
        const QString source = cover.source == Cover::Source::Embedded ? QStringLiteral("embedded")
                                                                       : QStringLiteral("file");
        out << QStringLiteral("source=%1").arg(source) << '\n'
            << QStringLiteral("file=%1").arg(cover.file) << '\n'
            << QStringLiteral("mime=%1").arg(cover.mimeType) << '\n'
            << QStringLiteral("type=%1").arg(cover.type) << '\n'
            << QStringLiteral("bytes=%1").arg(cover.byteCount) << '\n';
    }
    out.flush();
    return 0;
}

// CV-CLI: writes the cover's bytes to TARGET. Exit 3, writing nothing, when there is no cover,
// TARGET exists, or TARGET cannot be written.
int extractCover(const QString &file, const QString &target, bool fileFirst)
{
    const Cover cover = linuxbee::findCover(file, optionsFor(fileFirst, true));
    if (cover.source == Cover::Source::None)
        return reportFailure(QStringLiteral("no cover for %1").arg(file));
    // A link at OUT counts as existing, even one that leads nowhere: QFileInfo::exists follows it.
    if (QFileInfo::exists(target) || QFileInfo(target).isSymLink())
        return reportFailure(QStringLiteral("%1 exists").arg(target));

    // NewOnly: the open fails rather than follow or truncate a file that appeared since the check.
    QFile output(target);
    if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return reportFailure(
            QStringLiteral("cannot write %1: %2").arg(target, output.errorString()));
    // Success is the whole write, the flush and a clean close. Otherwise this run made OUT, so it
    // removes it again: nothing is left behind.
    const bool written = output.write(cover.data) == cover.data.size() && output.flush();
    const QString writeError = output.errorString();
    output.close();
    if (!written || output.error() != QFile::NoError) {
        QFile::remove(target);
        return reportFailure(QStringLiteral("cannot write %1: %2")
                .arg(target, written ? output.errorString() : writeError));
    }

    QTextStream out(stdout);
    out << QStringLiteral("wrote %1 bytes to %2").arg(cover.data.size()).arg(target) << '\n';
    out.flush();
    return 0;
}

int runCover(const QStringList &arguments)
{
    QCommandLineParser parser;
    parser.addOption(QCommandLineOption(QStringLiteral("file-first"),
        QStringLiteral("Try the folder's picture file before the embedded one.")));
    // parse(), not process(): process() exits 1 on an unknown option, and CV-CLI requires 2.
    if (!parser.parse(QStringList { QStringLiteral("linuxbeectl cover") } + arguments))
        return usageError(QStringLiteral("%1\n%2").arg(parser.errorText(), QLatin1String(kUsage)));

    const QStringList positional = parser.positionalArguments();
    if (positional.isEmpty())
        return usageError(QStringLiteral("linuxbeectl: cover needs show or extract.\n%1")
                .arg(QLatin1String(kUsage)));

    const QString subcommand = positional.at(0);
    const bool fileFirst = parser.isSet(QStringLiteral("file-first"));
    if (subcommand == QLatin1String("show")) {
        if (positional.size() != 2)
            return usageError(QStringLiteral("linuxbeectl: cover show takes exactly one file.\n%1")
                    .arg(QLatin1String(kUsage)));
        return showCover(positional.at(1), fileFirst);
    }
    if (subcommand == QLatin1String("extract")) {
        if (positional.size() != 3)
            return usageError(
                QStringLiteral("linuxbeectl: cover extract takes a file and a target.\n%1")
                    .arg(QLatin1String(kUsage)));
        return extractCover(positional.at(1), positional.at(2), fileFirst);
    }
    return usageError(QStringLiteral("linuxbeectl: cover has no subcommand %1.\n%2")
            .arg(QLatin1Char('"') + subcommand + QLatin1Char('"'), QLatin1String(kUsage)));
}

}

LINUXBEE_COMMAND(cover, "cover", "Show or extract a track's cover picture.", runCover)
