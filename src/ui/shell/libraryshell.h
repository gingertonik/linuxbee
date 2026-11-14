#pragma once
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

#include "library/library.h"
#include "playlists/playliststore.h"

class QSettings;
class QWidget;

// Puts the library on screen: the navigator in the left panel, the track
// list in the middle, and the File and Send To commands that act on them.
// Contract: docs/spec/library-shell.md (SH-x). It owns no rule of its own;
// it connects parts that do.
namespace linuxbee::ui {

class ActionRegistry;
class MainWindow;
class NavigatorPanel;
class TrackListModel;
class TrackListPage;

// What the shell must ask the outside world. Tests replace all three.
struct ShellHooks {
    std::function<QStringList()> chooseFolders; // for "Scan Folders"; empty: the user cancelled
    std::function<QStringList()> chooseFiles; // for "Add Files"
    std::function<QDateTime()> now; // UTC
};

class LibraryShell : public QObject {
    Q_OBJECT
public:
    // Nothing handed in is owned; all of it must outlive the shell.
    LibraryShell(MainWindow *window, ActionRegistry *actions, Library *library,
        PlaylistStore *playlists, QSettings *settings, ShellHooks hooks, QObject *parent = nullptr);
    ~LibraryShell() override;

    NavigatorPanel *navigator() const;
    TrackListModel *model() const;
    TrackListPage *trackList() const;

    QString currentNode() const; // SH-NODE
    QList<qint64> selectedTrackIds() const; // in the order shown
    QStringList selectedPaths() const;
    QStringList shownPaths() const; // every row of the list, in the order shown
    QString lastMessage() const; // SH-MESSAGE: what the status bar was last told

    // SH-NODE: what the "Playing Tracks" node shows. The player assembly calls
    // this whenever the play queue changes.
    void setPlayingPaths(const QStringList &paths);

    // SH-EXTEND: for the plans that add to what the shell shows.
    QList<LibraryTrack> nodeTracks() const; // what the node holds, before narrowing and search
    // Only tracks for which `keep` answers true are listed. Empty: all of them.
    void setNarrowing(std::function<bool(const LibraryTrack &)> keep);
    // A widget shown to the left of the track list. The shell owns it from
    // then on; another one, or null, deletes it.
    void setBrowser(QWidget *browser);
    QWidget *browser() const;
    // Asked, in the order added, for the tracks of a node before the shell
    // looks itself. True: `tracks` are the node's tracks.
    using NodeSource = std::function<bool(const QString &nodeId, QList<LibraryTrack> *tracks)>;
    void addNodeSource(NodeSource source);
    void reloadNode(); // the node's tracks are read again

    void saveState(); // SH-STORE

signals:
    // The user activated a track: `paths` is the list as shown, `index` the row.
    void playRequested(const QStringList &paths, int index);
    void nodeTracksChanged(); // SH-EXTEND: nodeTracks() may answer differently now

private:
    struct Private;
    Private *d;

    // Private helpers, defined in libraryshell.cpp (the state lives in
    // libraryshell_p.h; the commands are registered in
    // libraryshell_commands.cpp).
    void registerShellCommands();
    void onNodeSelected(const QString &nodeId);
    void showNode(); // list the chosen node's tracks (SH-NODE, SH-EXTEND)
    QList<LibraryTrack> readNodeTracks() const; // sources first, then SH-NODE
    QList<LibraryTrack> playlistTracks(const QString &id) const;
    QList<LibraryTrack> playingTracks() const;
    void refreshCounts(); // SH-COUNTS
    QStringList playlistIds() const;
    void onLibraryChanged(const QList<qint64> &ids); // SH-LIVE
    void onPlaylistsChanged(); // SH-LIVE
    void onPlaylistTracksChanged(const QString &id); // SH-LIVE
    void showMessage(const QString &text); // SH-MESSAGE
    void scanFolders(); // SH-COMMANDS
    void addFiles(); // SH-COMMANDS
    void removeSelected(); // SH-COMMANDS
    void sendToSection(const QString &section); // SH-COMMANDS
    QString targetSection() const; // the library section on show, else Music
    void refreshAll(); // view.refresh: counts, playlists, the node
};

}
