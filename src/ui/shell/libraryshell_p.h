#pragma once
// The shell's private state (plan 30). libraryshell.cpp builds the parts;
// libraryshell_commands.cpp acts on them. The struct is in its own header
// because the class is split over those two .cpp files.
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

#include "library/library.h"
#include "playlists/playliststore.h"
#include "shell/libraryshell.h"

class QSettings;
class QSplitter;
class QWidget;

namespace linuxbee::ui {

// Node id parts (SH-NODE): "library:" + section, "playlist:" + id, and
// "playing". Empty from sectionOf/playlistOf when the id is not of that kind.
extern const QString kLibraryPrefix;
extern const QString kPlaylistPrefix;
extern const QString kPlayingNode;
extern const QString kMusicSection;

QString sectionOf(const QString &nodeId);
QString playlistOf(const QString &nodeId);

class ActionRegistry;
class MainWindow;
class NavigatorPanel;
class TrackListModel;
class TrackListPage;

struct LibraryShell::Private {
    MainWindow *window = nullptr;
    ActionRegistry *actions = nullptr;
    Library *library = nullptr;
    PlaylistStore *playlists = nullptr;
    QSettings *settings = nullptr;
    ShellHooks hooks;

    NavigatorPanel *navigator = nullptr;
    TrackListModel *model = nullptr;
    TrackListPage *page = nullptr; // the widget handed to the window, named `tracks`
    QSplitter *splitter = nullptr; // inside the page (SH-EXTEND)
    QWidget *browser = nullptr;

    QString node; // the chosen node id (SH-NODE)
    QList<LibraryTrack> kept; // SH-EXTEND: the node's tracks as last read, before narrowing
    void switchOrder(); // SH-NODE: the model's given order for `node`; see its callers
    void showKept(); // the list shows those of `kept` that `narrowing` accepts; reads nothing
    QStringList playingPaths; // SH-NODE: what `playing` shows
    std::function<bool(const LibraryTrack &)> narrowing; // SH-EXTEND
    std::vector<NodeSource> nodeSources; // SH-EXTEND: asked first, in order
    QString message; // SH-MESSAGE: what the status bar was last told
    ScanRunner *scanner
        = nullptr; // SR-SHELL: the runner "Scan Folders" goes through; the shell is its parent
};

}
