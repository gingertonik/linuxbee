#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QPixmap>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

#include <cstring>

#include "actions/actionregistry.h"
#include "app/appinfo.h"
#include "mainwindow/mainwindow.h"

// W-MAIN (docs/spec/main-window.md): the settings file linuxbee.ini in the
// user's configuration folder, an ActionRegistry loaded from it, a MainWindow,
// and on exit the hotkeys saved. --version prints and leaves no trace;
// --screenshot FILE pictures the window for a person (and the gate) to judge.
int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--version") == 0) {
            // Print the version line and exit without opening a window or writing a file.
            QTextStream(stdout) << linuxbee::ui::versionLine() << '\n';
            return 0;
        }
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

    if (const int shot = application.arguments().indexOf(QStringLiteral("--screenshot"));
        shot >= 0 && shot + 1 < application.arguments().size()) {
        const QString file = application.arguments().at(shot + 1);
        // Give the event loop a moment so the window is laid out and drawn, then
        // save the picture and exit: 0 when it was saved, 3 when it could not be.
        QTimer::singleShot(300, [&window, file, &application] {
            const QPixmap pm = window.grab();
            const bool saved = pm.save(file, "PNG");
            application.exit(saved ? 0 : 3);
        });
    }

    return application.exec();
}
