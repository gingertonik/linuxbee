#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QTextStream>

#include "command.h"
#include "fields.h"
#include "paths/pathbuilder.h"

namespace {

using linuxbee::cli::usageError;

const char *const kUsage = "usage: linuxbeectl path build [--field NAME=VALUE]... "
                           "[--ext EXT] [--allow-missing] [--map C=TEXT]... "
                           "[--max-bytes N] TEMPLATE";

int runPath(const QStringList &arguments)
{
    QCommandLineParser parser;
    // Repeating --field and --map accumulate into parser.values().
    QCommandLineOption fieldOption(QStringLiteral("field"),
        QStringLiteral("Tag for the template, as NAME=VALUE. Repeatable."),
        QStringLiteral("NAME=VALUE"));
    QCommandLineOption extOption(QStringLiteral("ext"),
        QStringLiteral("Extension for the file name."), QStringLiteral("EXT"));
    QCommandLineOption allowOption(QStringLiteral("allow-missing"),
        QStringLiteral("Fill blank tags with Unknown <name> instead of refusing the track."));
    QCommandLineOption mapOption(QStringLiteral("map"),
        QStringLiteral("Replacement for the single character C; C= removes it. Repeatable."),
        QStringLiteral("C=TEXT"));
    QCommandLineOption maxOption(QStringLiteral("max-bytes"),
        QStringLiteral("Longest single folder or file name, in UTF-8 bytes (8 or more)."),
        QStringLiteral("N"));
    parser.addOptions({ fieldOption, extOption, allowOption, mapOption, maxOption });

    // parse(), not process(): process() exits 1 on an unknown option, and P-CLI
    // requires 2. The parser expects the program name first.
    if (!parser.parse(QStringList { QStringLiteral("linuxbeectl path") } + arguments))
        return usageError(parser.errorText());

    const QStringList positional = parser.positionalArguments();
    if (positional.isEmpty())
        return usageError(
            QStringLiteral("linuxbeectl: path needs build.\n%1").arg(QLatin1String(kUsage)));
    if (positional.at(0) != QLatin1String("build"))
        return usageError(QStringLiteral("linuxbeectl: path has no subcommand \"%1\".\n%2")
                .arg(positional.at(0), QLatin1String(kUsage)));
    if (positional.size() != 2)
        return usageError(QStringLiteral("linuxbeectl: path build takes exactly one template.\n%1")
                .arg(QLatin1String(kUsage)));

    linuxbee::PathRules rules;
    rules.skipWhenTagsMissing = !parser.isSet(allowOption);

    // P-CLI: --map C=TEXT sets the replacement for C; C= removes it.
    for (const QString &specification : parser.values(mapOption)) {
        const int equals = specification.indexOf(QLatin1Char('='));
        if (equals != 1)
            return usageError(QStringLiteral("linuxbeectl: --map needs C=TEXT"));
        rules.characterMap.insert(QChar(specification.at(0)), specification.mid(equals + 1));
    }

    if (parser.isSet(maxOption)) {
        bool isNumber = false;
        const int maxBytes = parser.value(maxOption).toInt(&isNumber);
        if (!isNumber || maxBytes < 8)
            return usageError(
                QStringLiteral("linuxbeectl: --max-bytes needs a whole number of 8 or more."));
        rules.maxComponentBytes = maxBytes;
    }

    linuxbee::cli::FieldSource fields;
    if (!linuxbee::cli::readFields(parser.values(fieldOption), &fields))
        return usageError(QStringLiteral("linuxbeectl: --field needs NAME=VALUE"));

    const linuxbee::Template pathTemplate = linuxbee::Template::parse(positional.at(1));
    const linuxbee::BuiltPath built
        = linuxbee::buildRelativePath(pathTemplate, fields, parser.value(extOption), rules);

    // P-CLI: status to stream and exit code.
    switch (built.status) {
    case linuxbee::BuiltPath::Status::Ok: {
        QTextStream out(stdout);
        out << built.relativePath << '\n';
        return 0;
    }
    case linuxbee::BuiltPath::Status::InvalidTemplate: {
        QTextStream out(stderr);
        out << QStringLiteral("error at %1: %2")
                   .arg(built.templateError.position)
                   .arg(built.templateError.message)
            << '\n';
        return 2;
    }
    case linuxbee::BuiltPath::Status::MissingTags: {
        QTextStream out(stderr);
        out << QStringLiteral("missing tags: %1")
                   .arg(built.missingFields.join(QStringLiteral(", ")))
            << '\n';
        return 3;
    }
    case linuxbee::BuiltPath::Status::EmptyName: {
        QTextStream out(stderr);
        out << QStringLiteral("linuxbeectl: the template leaves nothing for the file name.")
            << '\n';
        return 3;
    }
    }
    return 3; // not reached: every status is handled above
}

} // namespace

LINUXBEE_COMMAND(path, "path", "Build a relative file path from a naming template.", runPath)
