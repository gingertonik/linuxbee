#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QTextStream>

#include "command.h"
#include "fields.h"
#include "template/template.h"

namespace {

using linuxbee::cli::usageError;

const char *const kUsage = "usage: linuxbeectl template eval [--field NAME=VALUE]... "
                           "[--unknown-for-blank] TEMPLATE\n"
                           "       linuxbeectl template check TEMPLATE";

// T-CLI: an invalid template prints "error at N: message" on standard error,
// nothing on standard output, and exits 2.
int printParseError(const linuxbee::TemplateError &error)
{
    QTextStream out(stderr);
    out << QStringLiteral("error at %1: %2").arg(error.position).arg(error.message) << '\n';
    out.flush();
    return 2;
}

int runTemplate(const QStringList &arguments)
{
    QCommandLineParser parser;
    // Repeating --field accumulates into parser.values().
    QCommandLineOption fieldOption(QStringLiteral("field"),
        QStringLiteral("Field for the template, as NAME=VALUE. Repeatable."),
        QStringLiteral("NAME=VALUE"));
    QCommandLineOption blankOption(QStringLiteral("unknown-for-blank"),
        QStringLiteral("Write a blank field at the top level of the template as Unknown <name>."));
    parser.addOptions({ fieldOption, blankOption });

    // parse(), not process(): process() exits 1 on an unknown option, and T-CLI
    // requires 2. The parser expects the program name first.
    if (!parser.parse(QStringList { QStringLiteral("linuxbeectl template") } + arguments))
        return usageError(parser.errorText());

    const QStringList positional = parser.positionalArguments();
    if (positional.isEmpty())
        return usageError(QStringLiteral("linuxbeectl: template needs eval or check.\n%1")
                .arg(QLatin1String(kUsage)));
    const QString subcommand = positional.at(0);
    if (subcommand != QLatin1String("eval") && subcommand != QLatin1String("check"))
        return usageError(QStringLiteral("linuxbeectl: template has no subcommand \"%1\".\n%2")
                .arg(subcommand, QLatin1String(kUsage)));
    if (positional.size() != 2)
        return usageError(QStringLiteral("linuxbeectl: template %1 takes exactly one template.\n%2")
                .arg(subcommand, QLatin1String(kUsage)));

    // T-CLI: check prints OK for a valid template, eval prints the result of
    // filling it in.
    const linuxbee::Template tmpl = linuxbee::Template::parse(positional.at(1));
    if (!tmpl.isValid())
        return printParseError(tmpl.error());
    QTextStream out(stdout);
    if (subcommand == QLatin1String("check")) {
        out << QStringLiteral("OK") << '\n';
        return 0;
    }

    linuxbee::cli::FieldSource fields;
    if (!linuxbee::cli::readFields(parser.values(fieldOption), &fields))
        return usageError(QStringLiteral("linuxbeectl: --field needs NAME=VALUE"));

    linuxbee::TemplateOptions options;
    options.unknownForBlank = parser.isSet(blankOption);
    out << tmpl.evaluate(fields, options) << '\n';
    return 0;
}

}

LINUXBEE_COMMAND(template, "template", "Fill in or check a naming template.", runTemplate)
