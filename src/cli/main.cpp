#include <QCoreApplication>
#include <QTextStream>

#include "command.h"
#include "version.h"

namespace {

QString commandList()
{
    QString text = QStringLiteral("usage: linuxbeectl <command> [arguments]\n\ncommands:\n");
    const std::vector<linuxbee::cli::Command> all = linuxbee::cli::commands();
    for (const linuxbee::cli::Command &command : all)
        text += QStringLiteral("  %1 %2\n")
                    .arg(QLatin1String(command.name), -10)
                    .arg(QLatin1String(command.summary));
    text += QStringLiteral("\nlinuxbeectl --version prints the version.");
    return text;
}

}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("linuxbeectl"));
    QCoreApplication::setApplicationVersion(linuxbee::versionString());

    const QStringList arguments = app.arguments().mid(1);
    if (arguments.isEmpty())
        return linuxbee::cli::usageError(commandList());

    const QString first = arguments.at(0);
    QTextStream out(stdout);
    if (first == QLatin1String("--version") || first == QLatin1String("-v")) {
        out << QStringLiteral("linuxbeectl %1").arg(linuxbee::versionString()) << '\n';
        return 0;
    }
    if (first == QLatin1String("--help") || first == QLatin1String("-h")) {
        out << commandList() << '\n';
        return 0;
    }
    const std::vector<linuxbee::cli::Command> all = linuxbee::cli::commands();
    for (const linuxbee::cli::Command &command : all) {
        if (first == QLatin1String(command.name))
            return command.run(arguments.mid(1));
    }
    return linuxbee::cli::usageError(
        QStringLiteral("linuxbeectl: unknown command \"%1\".\n%2").arg(first, commandList()));
}
