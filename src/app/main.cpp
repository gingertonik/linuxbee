#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QPixmap>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

#include "actions/actionregistry.h"
#include "app/appinfo.h"
#include "mainwindow/mainwindow.h"

// W-MAIN (docs/spec/main-window.md): the settings file linuxbee.ini in the
// user's configuration folder, an ActionRegistry loaded from it, a MainWindow,
// and on exit the hotkeys saved. --version prints and leaves no trace;
// --screenshot FILE pictures the window for a person (and the gate) to judge.
// W-MAIN "Options": the whole command line is read before anything is done,
// and a mistake ends the program with a usage message and status 2.
int main(int argc, char *argv[])
{
    const QString usage
        = QStringLiteral("usage: linuxbee [--version] [--screenshot FILE] [FILE...]");
    bool wantVersion = false;
    QString shotFile;
    bool wantShot = false;
    for (int i = 1; i < argc; ++i) {
        const QString word = QString::fromLocal8Bit(argv[i]);
        if (word == QStringLiteral("--version")) {
            wantVersion = true;
        } else if (word == QStringLiteral("--screenshot")) {
            // A mistake: given twice, the last word, or followed by an option.
            QString problem;
            if (wantShot)
                problem = QStringLiteral("--screenshot given twice");
            else if (i + 1 >= argc)
                problem = QStringLiteral("--screenshot needs a file");
            else if (QString::fromLocal8Bit(argv[i + 1]).startsWith(QStringLiteral("--")))
                problem = QStringLiteral("--screenshot needs a file, not an option");
            if (!problem.isEmpty()) {
                QTextStream(stderr) << QStringLiteral("linuxbee: ") + problem << '\n'
                                    << usage << '\n';
                return 2;
            }
            shotFile = QString::fromLocal8Bit(argv[++i]); // the next word is the file
            wantShot = true;
        } else if (word.startsWith(QStringLiteral("--"))) {
            QTextStream(stderr) << QStringLiteral("linuxbee: unknown option ") + word << '\n'
                                << usage << '\n';
            return 2;
        }
        // Any other word is a file handed over by the desktop: accepted, and for now ignored.
    }
    if (wantVersion) {
        // Print the version line and exit without opening a window or writing a file.
        QTextStream(stdout) << linuxbee::ui::versionLine() << '\n';
        return 0;
    }

    QApplication application(argc, argv);
    linuxbee::ui::applyApplicationInfo(application);

    const QString iniPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        + QStringLiteral("/linuxbee.ini");
    QDir().mkpath(QFileInfo(iniPath).absolutePath());
    QSettings settings(iniPath, QSettings::IniFormat);

    linuxbee::ui::ActionRegistry registry;
    registry.load(settings);

    linuxbee::ui::MainWindow window(&registry, &settings);
    window.show();

    QObject::connect(&application, &QApplication::aboutToQuit, [&registry, &settings, &window] {
        // W-MAIN: on exit the hotkeys are saved, and the layout with them
        // (closing the window already saved it; a quit that skipped closing must not lose it).
        window.saveLayout();
        registry.save(settings);
        settings.sync();
    });

    if (wantShot) {
        // Give the event loop a moment so the window is laid out and drawn, then
        // save the picture and exit: 0 when it was saved, 3 when it could not be.
        QTimer::singleShot(300, [&window, shotFile, &application] {
            const QPixmap pm = window.grab();
            const bool saved = pm.save(shotFile, "PNG");
            application.exit(saved ? 0 : 3);
        });
    }

    return application.exec();
}
