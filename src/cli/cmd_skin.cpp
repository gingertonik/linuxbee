#include <QCommandLineParser>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include "command.h"
#include "skin/skin.h"

namespace {

using linuxbee::cli::usageError;

const char *const kUsage = "usage: linuxbeectl skin show FILE";

// SK-CLI: exit 3 when the file cannot be read or is not a skin.
constexpr int kFailed = 3;

int reportFailure(const QString &file, const QString &reason)
{
    QTextStream err(stderr);
    err << QStringLiteral("failed: %1: %2").arg(file, reason) << '\n';
    err.flush();
    return kFailed;
}

// SK-CLI: what the file alone gives; what it depends on is not read.
int showSkin(const QString &file)
{
    const QFileInfo info(file);
    if (info.exists() && !info.isFile())
        return reportFailure(file, QStringLiteral("it is not a file"));
    QFile input(file);
    if (!input.open(QIODevice::ReadOnly))
        return reportFailure(file, input.errorString());
    const linuxbee::Skin skin = linuxbee::readSkin(input.readAll());
    if (!skin.ok)
        return reportFailure(file, skin.error);

    const QMap<QString, linuxbee::SkinColour> colours = linuxbee::skinColours(skin);
    QTextStream out(stdout);
    if (!skin.dependsOn.isEmpty())
        out << QStringLiteral("depends on: %1").arg(skin.dependsOn) << '\n';
    // The map is in the order of the names' code points.
    for (auto it = colours.cbegin(); it != colours.cend(); ++it)
        out << QStringLiteral("%1 = %2").arg(it.key(), linuxbee::skinColourText(it.value()))
            << '\n';
    for (const QString &problem : skin.problems)
        out << QStringLiteral("problem: %1").arg(problem) << '\n';
    out << QStringLiteral("%1 colours, %2 elements, %3 problems")
               .arg(colours.size())
               .arg(skin.elements.size())
               .arg(skin.problems.size())
        << '\n';
    out.flush();
    return 0;
}

int runSkin(const QStringList &arguments)
{
    QCommandLineParser parser;
    // parse(), not process(): process() exits 1 on an unknown option, and SK-CLI requires 2.
    if (!parser.parse(QStringList { QStringLiteral("linuxbeectl skin") } + arguments))
        return usageError(QStringLiteral("%1\n%2").arg(parser.errorText(), QLatin1String(kUsage)));

    const QStringList positional = parser.positionalArguments();
    if (positional.isEmpty())
        return usageError(
            QStringLiteral("linuxbeectl: skin needs show.\n%1").arg(QLatin1String(kUsage)));
    const QString subcommand = positional.at(0);
    if (subcommand != QLatin1String("show"))
        return usageError(QStringLiteral("linuxbeectl: skin has no subcommand %1.\n%2")
                .arg(QLatin1Char('"') + subcommand + QLatin1Char('"'), QLatin1String(kUsage)));
    if (positional.size() != 2)
        return usageError(QStringLiteral("linuxbeectl: skin show takes exactly one file.\n%1")
                .arg(QLatin1String(kUsage)));
    return showSkin(positional.at(1));
}

}

LINUXBEE_COMMAND(
    skin, "skin", "Show the colours linuxBee takes from a MusicBee skin file.", runSkin)
