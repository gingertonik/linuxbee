#include "shell/libraryshell.h"
#include "shell/libraryshell_p.h"

#include "actions/actionregistry.h"
#include "mainwindow/mainwindow.h"
#include "navigator/navigatorpanel.h"
#include "tracklist/tracklistmodel.h"
#include "tracklist/tracklistpage.h"

#include <QList>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QWidget>

#include <algorithm>

// Plan 30: the library on screen. Contract: docs/spec/library-shell.md (SH-x).
// The shell owns no rule: it builds the parts, feeds the navigator, and keeps
// the list following the node, the library and the playlists. The commands
// and their messages live in libraryshell_commands.cpp.
namespace linuxbee::ui {

// The node id parts are declared in libraryshell_p.h: both compilation
// units read nodes (SH-NODE).
const QString kLibraryPrefix = QStringLiteral("library:");
const QString kPlaylistPrefix = QStringLiteral("playlist:");
const QString kPlayingNode = QStringLiteral("playing");
const QString kMusicSection = QStringLiteral("Music");

QString sectionOf(const QString &nodeId)
{
    return nodeId.startsWith(kLibraryPrefix) ? nodeId.mid(kLibraryPrefix.size()) : QString();
}

QString playlistOf(const QString &nodeId)
{
    return nodeId.startsWith(kPlaylistPrefix) ? nodeId.mid(kPlaylistPrefix.size()) : QString();
}

LibraryShell::LibraryShell(MainWindow *window, ActionRegistry *actions, Library *library,
    PlaylistStore *playlists, QSettings *settings, ShellHooks hooks, QObject *parent)
    : QObject(parent)
    , d(new Private)
{
    d->window = window;
    d->actions = actions;
    d->library = library;
    d->playlists = playlists;
    d->settings = settings;
    d->hooks = std::move(hooks);

    // SH-BUILD: the navigator in the left panel. Created without a parent and
    // handed over: the window owns it from then on.
    d->navigator = new NavigatorPanel;
    d->window->setPanelWidget(MainWindow::Panel::Navigator, d->navigator);
    refreshCounts();
    d->navigator->setPlaylists(playlistIds());

    // SH-BUILD: the model with its columns and sorting, the page over it,
    // in a page the window shows.
    d->model = new TrackListModel(this);
    d->model->load(*d->settings, QStringLiteral("tracklist"));
    d->page = new TrackListPage(d->model, d->actions);
    d->page->setObjectName(QStringLiteral("tracks"));
    d->window->addPage(QStringLiteral("tracks"), d->page);
    d->window->showPage(QStringLiteral("tracks"));

    // SH-BUILD: the commands first (they name the shell as their context).
    registerShellCommands();

    // SH-BUILD: the stored node, or library:Music when there is none or it
    // no longer exists. Choosing happens before nodeSelected is connected,
    // so the first load is this explicit showNode().
    const QString stored = d->settings->value(QStringLiteral("shell/node")).toString();
    if (!stored.isEmpty() && d->navigator->selectNode(stored))
        d->node = d->navigator->currentNode();
    else
        d->node = d->navigator->currentNode();
    connect(d->navigator, &NavigatorPanel::nodeSelected, this, &LibraryShell::onNodeSelected);
    showNode();

    // SH-LIVE: whoever changes the library or the playlists, what is on show
    // follows.
    connect(d->library, &Library::tracksAdded, this, &LibraryShell::onLibraryChanged);
    connect(d->library, &Library::tracksUpdated, this, &LibraryShell::onLibraryChanged);
    connect(d->library, &Library::tracksRemoved, this, &LibraryShell::onLibraryChanged);
    connect(d->playlists, &PlaylistStore::listChanged, this, &LibraryShell::onPlaylistsChanged);
    connect(
        d->playlists, &PlaylistStore::tracksChanged, this, &LibraryShell::onPlaylistTracksChanged);

    // SH-PLAY: an activated track that is in the list asks for the list,
    // as shown now, from its row. A track not in the list emits nothing.
    connect(d->page, &TrackListPage::trackActivated, this, [this](qint64 trackId) {
        const int row = d->model->rowOf(trackId);
        if (row < 0)
            return;
        emit playRequested(shownPaths(), row);
    });
}

LibraryShell::~LibraryShell()
{
    delete d;
}

NavigatorPanel *LibraryShell::navigator() const
{
    return d->navigator;
}

TrackListModel *LibraryShell::model() const
{
    return d->model;
}

TrackListPage *LibraryShell::trackList() const
{
    return d->page;
}

QString LibraryShell::currentNode() const
{
    return d->node;
}

QList<qint64> LibraryShell::selectedTrackIds() const
{
    return d->page->selectedTrackIds(); // in the order shown
}

QStringList LibraryShell::selectedPaths() const
{
    QStringList out;
    for (const qint64 id : selectedTrackIds()) {
        const auto track = d->library->track(id);
        out.append(track ? track->path : QString());
    }
    return out;
}

QStringList LibraryShell::shownPaths() const
{
    QStringList out;
    const int rows = d->model->rowCount();
    out.reserve(rows);
    for (int row = 0; row < rows; ++row) {
        const auto track = d->library->track(d->model->trackId(row));
        out.append(track ? track->path : QString());
    }
    return out;
}

QString LibraryShell::lastMessage() const
{
    return d->message;
}

void LibraryShell::setPlayingPaths(const QStringList &paths)
{
    d->playingPaths = paths;
    if (d->node == kPlayingNode) // SH-NODE: while on show, read the tracks again
        showNode();
}

QList<LibraryTrack> LibraryShell::nodeTracks() const
{
    return readNodeTracks();
}

void LibraryShell::setNarrowing(std::function<bool(const LibraryTrack &)> keep)
{
    d->narrowing = std::move(keep);
    showNode();
}

void LibraryShell::setBrowser(QWidget *browser)
{
    // SH-EXTEND: the widget to the left of the track list. The shell owns it;
    // the one before goes at once. The same widget again changes nothing.
    if (browser == d->browser)
        return;
    QSplitter *split = d->splitter;
    QWidget *old = d->browser;
    d->browser = browser;
    if (browser) {
        split->insertWidget(0, browser); // to the left of the list
        split->setSizes({ 220, split->widget(1)->width() });
    }
    delete old;
}

QWidget *LibraryShell::browser() const
{
    return d->browser;
}

void LibraryShell::addNodeSource(NodeSource source)
{
    d->nodeSources.push_back(std::move(source));
    showNode(); // SH-EXTEND: adding a source reads the chosen node again
}

void LibraryShell::reloadNode()
{
    showNode(); // the search text is the user's; it stays
}

void LibraryShell::saveState()
{
    // SH-STORE: the model to `tracklist`, the chosen node to `shell/node`.
    d->model->save(*d->settings, QStringLiteral("tracklist"));
    d->settings->setValue(QStringLiteral("shell/node"), d->node);
}

void LibraryShell::onNodeSelected(const QString &nodeId)
{
    d->page->setSearchText(QString()); // SH-NODE: a new node starts with an empty search
    d->node = nodeId;
    showNode();
}

void LibraryShell::showNode()
{
    QList<LibraryTrack> tracks = readNodeTracks();
    if (d->narrowing) {
        const auto keep = d->narrowing;
        tracks.erase(std::remove_if(tracks.begin(), tracks.end(),
                         [&keep](const LibraryTrack &t) { return !keep(t); }),
            tracks.end());
    }
    d->model->setTracks(tracks);
    emit nodeTracksChanged();
}

QList<LibraryTrack> LibraryShell::readNodeTracks() const
{
    // SH-EXTEND: the sources are asked first, in the order added; the first
    // that answers true has given the node's tracks.
    for (const NodeSource &source : d->nodeSources) {
        if (source) {
            QList<LibraryTrack> tracks;
            if (source(d->node, &tracks))
                return tracks;
        }
    }
    if (const QString section = sectionOf(d->node); !section.isEmpty())
        return d->library->tracks(section); // SH-NODE
    if (const QString playlist = playlistOf(d->node); !playlist.isEmpty())
        return playlistTracks(playlist);
    if (d->node == kPlayingNode)
        return playingTracks();
    return { };
}

QList<LibraryTrack> LibraryShell::playlistTracks(const QString &id) const
{
    // SH-NODE: each path looked up in the library; a path the library does
    // not know is passed over; a track is listed once.
    QList<LibraryTrack> out;
    const QStringList paths = d->playlists->tracks(id);
    QList<qint64> seen;
    for (const QString &path : paths) {
        const auto track = d->library->trackByPath(path);
        if (!track)
            continue;
        if (seen.contains(track->id))
            continue;
        seen.append(track->id);
        out.append(*track);
    }
    return out;
}

QList<LibraryTrack> LibraryShell::playingTracks() const
{
    QList<LibraryTrack> out;
    QList<qint64> seen;
    for (const QString &path : d->playingPaths) {
        const auto track = d->library->trackByPath(path);
        if (!track)
            continue;
        if (seen.contains(track->id))
            continue;
        seen.append(track->id);
        out.append(*track);
    }
    return out;
}

void LibraryShell::refreshCounts()
{
    // SH-COUNTS: every section, how many tracks it holds.
    QMap<QString, int> counts;
    for (const QString &section : Library::categories())
        counts.insert(section, d->library->tracks(section).size());
    d->navigator->setLibraryCounts(counts);
}

QStringList LibraryShell::playlistIds() const
{
    QStringList ids;
    for (const PlaylistInfo &playlist : d->playlists->playlists())
        ids.append(playlist.id);
    return ids;
}

void LibraryShell::onLibraryChanged(const QList<qint64> &ids)
{
    refreshCounts(); // SH-COUNTS: kept up to date on every library change
    // SH-EXTEND (task 8): a node a source answers for is read again on
    // every change of the library.
    for (const NodeSource &source : d->nodeSources) {
        if (source && source(d->node, nullptr)) {
            showNode();
            return;
        }
    }
    if (const QString section = sectionOf(d->node); !section.isEmpty()) {
        applyLibraryDelta(ids, section);
        return;
    }
    // SH-LIVE: on a playlist node or on `playing`, the node's tracks are
    // read again.
    showNode();
}

void LibraryShell::applyLibraryDelta(const QList<qint64> &ids, const QString &section)
{
    // SH-LIVE: each track named that belongs to the section on show is added
    // or replaced in the list; each that does not (it moved to another
    // section) is taken out. The rest of the list, and the selection, are
    // left alone: upsert/remove, never setTracks.
    QList<LibraryTrack> intoView;
    QList<qint64> outOfView;
    for (const qint64 id : ids) {
        const auto track = d->library->track(id);
        if (track && track->category == section)
            intoView.append(*track);
        else
            outOfView.append(id);
    }
    if (!intoView.isEmpty())
        d->model->upsertTracks(intoView);
    if (!outOfView.isEmpty())
        d->model->removeTracks(outOfView);
}

void LibraryShell::onPlaylistsChanged()
{
    // SH-LIVE lands in task 6.
}

void LibraryShell::onPlaylistTracksChanged(const QString &id)
{
    Q_UNUSED(id);
    // SH-LIVE lands in task 6.
}

}
