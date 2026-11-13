#include <QCommandLineParser>
#include <QCoreApplication>

#include "version.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("linuxbeectl"));
    QCoreApplication::setApplicationVersion(linuxbee::versionString());

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("linuxBee command-line tools"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);
    return 0;
}
