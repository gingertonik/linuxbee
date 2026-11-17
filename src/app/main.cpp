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
#include "app/quitonsignal.h"
#include "desktop/mprisservice.h"
#include "library/library.h"
#include "mainwindow/mainwindow.h"
#include "playlists/autoplayliststore.h"
#include "playlists/playliststore.h"
#include "audio/devicesink.h"
#include "playback/audioengine.h"
#include "shell/albumviewing.h"
#include "shell/artistbrowsing.h"
#include "shell/autoplaylistnodes.h"
#include "shell/backgroundshell.h"
#include "shell/librarypreferences.h"
#include "shell/libraryshell.h"
#include "shell/playbackshell.h"
#include "shell/playlistediting.h"
#include "shell/scanprogress.h"
#include "shell/toolsshell.h"
#include "skins/skinapplier.h"
#include "skins/skinchoosing.h"

// W-MAIN (docs/spec/main-window.md): the settings file linuxbee.ini in the
// user's configuration folder, an ActionRegistry loaded from it, a MainWindow,
// and on exit the hotkeys saved. --version prints and leaves no trace;
// --screenshot FILE pictures the window for a person (and the gate) to judge.
// --commands (TS-MAIN) prints the id of every implemented command and leaves.
// AB-MAIN (docs/spec/artist-browser.md): --parts prints whether the artist browser
// is shown, after everything is built, and leaves.
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
    const QString usage = QStringLiteral("usage: linuxbee [--version] [--commands] [--services] "
                                         "[--pages] [--parts] [--screenshot FILE] [FILE...]");
    bool wantVersion = false;
    bool wantCommands = false;
    bool wantServices = false;
    bool wantPages = false;
    bool wantParts = false;
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
        } else if (word == QStringLiteral("--pages")) {
            wantPages = true;
        } else if (word == QStringLiteral("--parts")) {
            wantParts = true;
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

    // SK-MAIN (docs/spec/skins.md): the stored skin is applied to the application at once, made
    // before the window, so the window is drawn in it.
    linuxbee::ui::SkinApplier skinApplier(&application, &settings);

    linuxbee::ui::MainWindow window(&registry, &settings);

    // SC-MAIN (docs/spec/skins.md): View > Skins, over the window, the registry and the applier,
    // with the folder `skins` in the data folder (made when a skin is added, not here).
    linuxbee::ui::SkinChoosing skinChoosing(&window, &registry, &skinApplier,
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/skins"));

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

    // AN-MAIN: the auto-playlists in the folder `auto-playlists` beside `playlists`, listed in the
    // navigator and shown by the shell with the default hooks; declared after the shell, so they
    // go before it.
    linuxbee::AutoPlaylistStore autoPlaylists(dataDir + QStringLiteral("/auto-playlists"));
    linuxbee::ui::AutoPlaylistNodes autoPlaylistNodes(
        &window, &shell, library.get(), &autoPlaylists);

    // AB-MAIN: the artist browser beside the track list, over the library shell;
    // it is destroyed before the shell.
    linuxbee::ui::ArtistBrowsing artistBrowsing(&shell, &settings);

    // AV-MAIN (docs/spec/album-view.md): the Album and Tracks view in the library page, over the
    // library shell and the settings; declared after the shell, so it is destroyed before it.
    linuxbee::ui::AlbumViewing albumViewing(&shell, &settings);

    // PE-MAIN: the playlist commands of the navigator's right click and File > Playlists > New
    // Playlist, with the window's own dialogs; declared after the shell, it goes before the shell.
    linuxbee::ui::PlaylistEditing playlistEditing(&window, &registry, &shell, &playlists);

    // TS-MAIN: the tools shell, with the default presenter (the dialogs run modally).
    linuxbee::ui::ToolsShell tools(&window, &registry, library.get(), &shell, &settings);

    // PS-MAIN: the real sound: the device sink, the ffmpeg engine over it, and
    // the playback shell over the engine. On the way out the playback shell
    // goes first, while the engine and the sink still exist.
    linuxbee::ui::DeviceSink sink;
    linuxbee::FfmpegAudioBackend backend(&sink);
    auto playback = std::make_unique<linuxbee::ui::PlaybackShell>(
        &window, &registry, library.get(), &shell, &backend, &settings, hooks.now);

    // SP-MAIN (docs/spec/scan-runner.md): the scan's row in the status bar and File > Stop Scan,
    // with the default question; declared after the shell whose runner it watches, so it goes
    // before it.
    linuxbee::ui::ScanProgress scanProgress(&window, &registry);
    scanProgress.watch(shell.scanner());

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
    scanProgress.watch(background->scanner()); // SP-MAIN: the folder monitor's scans too

    // LP-MAIN: the Library page joins the preferences, with the default folder chooser.
    linuxbee::ui::installLibraryPreferences(&tools, background.get(), &settings);

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
    if (wantPages) {
        // LP-MAIN: the title of every preferences page, in order, with no window.
        QTextStream out(stdout);
        for (const QString &title : tools.preferencesPages())
            out << title << '\n';
        out.flush();
        return 0;
    }
    if (wantParts) {
        // AB-MAIN: whether the artist browser is on show, with no window. Later plans add lines of
        // their own.
        QTextStream out(stdout);
        out << (artistBrowsing.isShown() ? QStringLiteral("artist-browser=shown")
                                         : QStringLiteral("artist-browser=hidden"))
            << '\n';
        // AN-MAIN: how many auto-playlists the store holds.
        out << QStringLiteral("auto-playlists=%1").arg(autoPlaylists.names().size()) << '\n';
        // AV-MAIN: which view the library page starts on, albums or tracks.
        out << QStringLiteral("main-view=%1").arg(albumViewing.mode()) << '\n';
        // SK-MAIN: the skin by the name of its file, or none when there is no skin.
        const QString skinFile = skinApplier.currentFile();
        out << (skinFile.isEmpty() ? QStringLiteral("skin=none")
                                   : QStringLiteral("skin=%1").arg(QFileInfo(skinFile).fileName()))
            << '\n';
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

    linuxbee::ui::quitOnSignals(&application); // W-SIGNAL
    return application.exec();
}
