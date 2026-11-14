#include <QApplication>
#include <QDBusConnection>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPixmap>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

#include <memory>

#include "actions/actionregistry.h"
#include "app/appinfo.h"
#include "desktop/mprisservice.h"
#include "library/library.h"
#include "mainwindow/mainwindow.h"
#include "playlists/playliststore.h"
#include "audio/devicesink.h"
#include "playback/audioengine.h"
#include "shell/backgroundshell.h"
#include "shell/libraryshell.h"
#include "shell/playbackshell.h"
#include "shell/toolsshell.h"

// W-MAIN (docs/spec/main-window.md): the settings file linuxbee.ini in the
// user's configuration folder, an ActionRegistry loaded from it, a MainWindow,
// and on exit the hotkeys saved. --version prints and leaves no trace;
// --screenshot FILE pictures the window for a person (and the gate) to judge.
// --commands (TS-MAIN) prints the id of every implemented command and leaves.
// W-MAIN "Options": the whole command line is read before anything is done,
// and a mistake ends the program with a usage message and status 2.
// SH-MAIN (docs/spec/library-shell.md): the library (library.db in the data
// folder, playlists beside it) is opened, the shell is built over the window,
// the file choosers and the clock come in through the hooks, and saveState,
// the hotkeys and sync() go on the way out. A library that cannot be opened
// ends with a line on standard error containing `library cannot be opened`,
// the same in a message box (unless --screenshot), and status 4; the file is
// not touched and no window is shown.
int main(int argc, char *argv[])
{
    const QString usage = QStringLiteral(
        "usage: linuxbee [--version] [--commands] [--services] [--screenshot FILE] [FILE...]");
    bool wantVersion = false;
    bool wantCommands = false;
    bool wantServices = false;
    QString shotFile;
    bool wantShot = false;
    for (int i = 1; i < argc; ++i) {
        const QString word = QString::fromLocal8Bit(argv[i]);
        if (word == QStringLiteral("--version")) {
            wantVersion = true;
        } else if (word == QStringLiteral("--commands")) {
            wantCommands = true;
        } else if (word == QStringLiteral("--services")) {
            wantServices = true;
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

    // SH-MAIN: library.db in the data folder, the playlists in the folder
    // `playlists` beside it; missing folders are created.
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    const QString dbPath = dataDir + QStringLiteral("/library.db");
    QString libraryError;
    auto library = linuxbee::Library::open(dbPath, &libraryError);
    if (!library) {
        const QString reason
            = QCoreApplication::translate("main", "linuxbee: library cannot be opened: %1")
                  .arg(libraryError);
        QTextStream(stderr) << reason << '\n';
        if (!wantShot) // a screenshot run has no person to answer a dialog
            QMessageBox::critical(nullptr, linuxbee::ui::applicationDisplayName(), reason);
        return 4; // the file is not touched and no window is shown
    }
    QDir().mkpath(dataDir + QStringLiteral("/playlists"));
    linuxbee::PlaylistStore playlists(dataDir + QStringLiteral("/playlists"));

    linuxbee::ui::ActionRegistry registry;
    registry.load(settings);

    linuxbee::ui::MainWindow window(&registry, &settings);

    // SH-MAIN: the choosers start in the user's music folder; `now` is UTC.
    const QString startDir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    linuxbee::ui::ShellHooks hooks;
    hooks.chooseFolders = [startDir] {
        // One folder, as a list: the shell scans every folder it is given,
        // and an empty answer means the user cancelled (SH-COMMANDS).
        const QString folder = QFileDialog::getExistingDirectory(
            nullptr, QCoreApplication::translate("main", "Choose folders to scan"), startDir);
        return folder.isEmpty() ? QStringList() : QStringList({ folder });
    };
    hooks.chooseFiles = [startDir] {
        return QFileDialog::getOpenFileNames(
            nullptr, QCoreApplication::translate("main", "Choose files to add"), startDir);
    };
    hooks.now = [] { return QDateTime::currentDateTimeUtc(); };

    // The shell is handed the window, the registry, the library, the
    // playlists, the settings and the hooks; it is destroyed before the
    // window, whose panels it fills.
    linuxbee::ui::LibraryShell shell(
        &window, &registry, library.get(), &playlists, &settings, hooks);

    // TS-MAIN: the tools shell, with the default presenter (the dialogs run modally).
    linuxbee::ui::ToolsShell tools(&window, &registry, library.get(), &shell, &settings);

    // PS-MAIN: the real sound: the device sink, the ffmpeg engine over it, and
    // the playback shell over the engine. On the way out the playback shell
    // goes first, while the engine and the sink still exist.
    linuxbee::ui::DeviceSink sink;
    linuxbee::FfmpegAudioBackend backend(&sink);
    auto playback = std::make_unique<linuxbee::ui::PlaybackShell>(
        &window, &registry, library.get(), &shell, &backend, &settings, hooks.now);

    if (wantCommands) {
        // TS-MAIN: every command that has its behaviour, in catalogue order, with no window.
        QTextStream out(stdout);
        for (const QString &id : registry.ids()) {
            if (registry.isImplemented(id))
                out << id << '\n';
        }
        out.flush();
        return 0;
    }
    // BG-MAIN: the background shell on the session bus. Closing it from the desktop
    // closes the window. Its clock is the one the other shells use. It is made after the
    // --commands answer, so --commands never touches the bus.
    linuxbee::ui::BackgroundShell::Hooks backgroundHooks;
    backgroundHooks.now = hooks.now;
    backgroundHooks.quit = [&window] { window.close(); };
    auto background = std::make_unique<linuxbee::ui::BackgroundShell>(&window, library.get(),
        playback.get(), &settings, backgroundHooks, QDBusConnection::sessionBus());

    if (wantServices) {
        // BG-MAIN: what the shell found, with no window: whether the media keys are on,
        // and how many folders are monitored. The program ends with status 0.
        QTextStream out(stdout);
        out << (background->mpris()->isRunning() ? QStringLiteral("mpris=on")
                                                 : QStringLiteral("mpris=off"))
            << '\n';
        out << QStringLiteral("monitored=%1").arg(background->monitoredFolders().size()) << '\n';
        out.flush();
        return 0;
    }
    window.show();

    QObject::connect(&application, &QApplication::aboutToQuit, [&] {
        // W-MAIN: on exit the hotkeys are saved, and the layout with them
        // (closing the window already saved it; a quit that skipped closing
        // must not lose it). SH-MAIN: the shell's state goes first, then the
        // hotkeys, then sync(). PS-MAIN: the playback shell's state goes
        // before all of it, and the playback shell is destroyed (stopping the sound).
        // BG-MAIN: the background shell goes first, so the bus is left before anything is saved.
        background.reset();
        playback->saveState();
        playback.reset();
        window.saveLayout();
        shell.saveState();
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
