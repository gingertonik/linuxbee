#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <optional>

#include "template/template.h"
#include "version.h"

namespace {

// The fields given with --field. A name nobody supplied is "no such field", so
// an unsupplied field prints as it was written (T-UNKNOWN).
class FieldSource : public linuxbee::TagSource {
public:
    std::optional<QString> value(const QString &fieldName) const override
    {
        const auto found = m_fields.constFind(fieldName);
        if (found == m_fields.constEnd())
            return std::nullopt;
        return found.value();
    }
    void set(const QString &name, const QString &value) { m_fields.insert(name, value); }

private:
    QHash<QString, QString> m_fields;
};

// T-CLI: a usage mistake prints a message on standard error, nothing on
// standard output, and exits 2.
int usageError(const QString &message)
{
    QTextStream out(stderr);
    out << message << '\n';
    out.flush();
    return 2;
}

// T-CLI: NAME is the text before the first '=', VALUE everything after it,
// which may be empty. A missing '=' or an empty NAME is a usage mistake.
bool readFields(const QStringList &specifications, FieldSource *fields)
{
    for (const QString &specification : specifications) {
        const qsizetype split = specification.indexOf(QLatin1Char('='));
        if (split <= 0)
            return false;
        fields->set(specification.left(split), specification.mid(split + 1));
    }
    return true;
}

// T-CLI: an invalid template prints "error at N: message" on standard error,
// nothing on standard output, and exits 2.
int printParseError(const linuxbee::TemplateError &error)
{
    QTextStream out(stderr);
    out << QStringLiteral("error at %1: %2").arg(error.position).arg(error.message) << '\n';
    out.flush();
    return 2;
}

const char *const kUsage = "usage: linuxbeectl template eval [--field NAME=VALUE]... "
                           "[--unknown-for-blank] TEMPLATE\n"
                           "       linuxbeectl template check TEMPLATE";

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("linuxbeectl"));
    QCoreApplication::setApplicationVersion(linuxbee::versionString());

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("linuxBee command-line tools"));
    parser.addHelpOption();
    parser.addVersionOption();
    // Repeating --field accumulates into parser.values(): Qt 6.12 has no
    // QCommandLineOption::MultiplePositions flag, it accumulates by default.
    QCommandLineOption fieldOption(QStringLiteral("field"),
        QStringLiteral("Field for the template, as NAME=VALUE. Repeatable."),
        QStringLiteral("NAME"));
    QCommandLineOption blankOption(QStringLiteral("unknown-for-blank"),
        QStringLiteral("Write a blank field at the top level of the template as Unknown <name>."));
    parser.addOptions({ fieldOption, blankOption });
    parser.addPositionalArgument(QStringLiteral("command"),
        QStringLiteral("The subsystem: template."), QStringLiteral("<command>"));
    parser.addPositionalArgument(QStringLiteral("subcommand"), QStringLiteral("eval or check."),
        QStringLiteral("[eval|check]"));
    parser.addPositionalArgument(QStringLiteral("template"), QStringLiteral("The template text."),
        QStringLiteral("[TEMPLATE]"));

    // parse(), not process(): process() exits 1 on an unknown option, and T-CLI
    // requires 2, so the exit code has to stay ours. --help and --version work
    // as before: parse() marks them and does not exit.
    if (!parser.parse(app.arguments()))
        return usageError(parser.errorText());
    if (parser.isSet(QStringLiteral("help")))
        parser.showHelp(0);
    if (parser.isSet(QStringLiteral("version")))
        parser.showVersion();

    const QStringList positional = parser.positionalArguments();
    if (positional.size() < 1 || positional.at(0) != QStringLiteral("template"))
        return usageError(
            QStringLiteral("linuxbeectl: unknown command.\n%1").arg(QLatin1String(kUsage)));
    if (positional.size() < 2)
        return usageError(QStringLiteral("linuxbeectl: template needs eval or check.\n%1")
                .arg(QLatin1String(kUsage)));
    const QString subcommand = positional.at(1);
    if (subcommand != QLatin1String("eval") && subcommand != QLatin1String("check"))
        return usageError(QStringLiteral("linuxbeectl: template has no subcommand \"%1\".\n%2")
                .arg(subcommand, QLatin1String(kUsage)));
    if (positional.size() != 3)
        return usageError(QStringLiteral("linuxbeectl: template %1 takes exactly one template.\n%2")
                .arg(subcommand, QLatin1String(kUsage)));

    // T-CLI: check prints OK for a valid template, eval prints the result of
    // filling it in.
    const linuxbee::Template tmpl = linuxbee::Template::parse(positional.at(2));
    if (!tmpl.isValid())
        return printParseError(tmpl.error());
    if (subcommand == QLatin1String("check")) {
        QTextStream out(stdout);
        out << QStringLiteral("OK") << '\n';
        out.flush();
        return 0;
    }

    FieldSource fields;
    if (!readFields(parser.values(fieldOption), &fields))
        return usageError(QStringLiteral("linuxbeectl: --field needs NAME=VALUE"));

    linuxbee::TemplateOptions options;
    options.unknownForBlank = parser.isSet(blankOption);
    QTextStream out(stdout);
    out << tmpl.evaluate(fields, options) << '\n';
    out.flush();
    return 0;
}
