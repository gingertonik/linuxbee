#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDateTime>
#include <QTextStream>

#include "command.h"
#include "fields.h"
#include "rules/rules.h"
#include "rules/values.h"

namespace {

using linuxbee::cli::usageError;

const char *const kUsage
    = "usage: linuxbeectl rules check JSON\n"
      "       linuxbeectl rules match [--field NAME=VALUE]... [--now WHEN] JSON\n"
      "       linuxbeectl rules operators";

// U-VALID: each problem on its own line on standard error, nothing on
// standard output, exit 2.
int printProblems(const QStringList &problems)
{
    QTextStream err(stderr);
    for (const QString &problem : problems)
        err << problem << '\n';
    err.flush();
    return 2;
}

int runRules(const QStringList &arguments)
{
    QCommandLineParser parser;
    // Repeating --field accumulates into parser.values().
    QCommandLineOption fieldOption(QStringLiteral("field"),
        QStringLiteral("Field of the track, as NAME=VALUE. Repeatable."),
        QStringLiteral("NAME=VALUE"));
    QCommandLineOption nowOption(QStringLiteral("now"),
        QStringLiteral("The moment for in-the-last, as an ISO 8601 date and time. Default: now."),
        QStringLiteral("WHEN"));
    parser.addOptions({ fieldOption, nowOption });

    // parse(), not process(): process() exits 1 on an unknown option, and U-CLI requires 2.
    if (!parser.parse(QStringList { QStringLiteral("linuxbeectl rules") } + arguments))
        return usageError(parser.errorText());

    const QStringList positional = parser.positionalArguments();
    if (positional.isEmpty())
        return usageError(QStringLiteral("linuxbeectl: rules needs check, match or operators.\n%1")
                .arg(QLatin1String(kUsage)));
    const QString subcommand = positional.at(0);
    if (subcommand != QLatin1String("check") && subcommand != QLatin1String("match")
        && subcommand != QLatin1String("operators"))
        return usageError(QStringLiteral("linuxbeectl: rules has no subcommand \"%1\".\n%2")
                .arg(subcommand, QLatin1String(kUsage)));
    if (subcommand == QLatin1String("operators")) {
        // U-CLI: every operator in the order of U-NAMES, its id, a tab, what a person reads.
        if (positional.size() != 1)
            return usageError(QStringLiteral("linuxbeectl: rules operators takes no argument.\n%1")
                    .arg(QLatin1String(kUsage)));
        QTextStream list(stdout);
        for (int n = static_cast<int>(linuxbee::RuleOperator::Is);
            n <= static_cast<int>(linuxbee::RuleOperator::DoesNotMatchCase); ++n) {
            const auto op = static_cast<linuxbee::RuleOperator>(n);
            list << linuxbee::ruleOperatorId(op) << '\t' << linuxbee::ruleOperatorLabel(op) << '\n';
        }
        list.flush();
        return 0;
    }
    if (positional.size() != 2)
        return usageError(QStringLiteral("linuxbeectl: rules %1 takes exactly one rule set.\n%2")
                .arg(subcommand, QLatin1String(kUsage)));

    // U-CLI: JSON that does not load is refused on standard error with exit 2.
    linuxbee::RuleSet rules;
    QString error;
    if (!linuxbee::rulesFromJson(positional.at(1), &rules, &error))
        return usageError(error);
    const QStringList problems = linuxbee::validateRules(rules);
    if (!problems.isEmpty())
        return printProblems(problems);

    QTextStream out(stdout);
    if (subcommand == QLatin1String("check")) {
        out << linuxbee::rulesToJson(rules) << '\n';
        out.flush();
        return 0;
    }

    linuxbee::cli::FieldSource fields;
    if (!linuxbee::cli::readFields(parser.values(fieldOption), &fields))
        return usageError(QStringLiteral("linuxbeectl: --field needs NAME=VALUE"));

    linuxbee::RuleContext context;
    if (parser.isSet(nowOption)) {
        const auto when = linuxbee::rules::readDate(parser.value(nowOption));
        if (!when)
            return usageError(QStringLiteral("linuxbeectl: --now needs an ISO 8601 date and time"));
        context.now = *when;
    } else {
        context.now = QDateTime::currentDateTimeUtc();
    }

    const bool matched = linuxbee::matchesRules(rules, fields, context);
    out << (matched ? QStringLiteral("match") : QStringLiteral("no match")) << '\n';
    out.flush();
    return matched ? 0 : 1;
}

}

LINUXBEE_COMMAND(rules, "rules", "Check a rule set, or test a track against it.", runRules)
