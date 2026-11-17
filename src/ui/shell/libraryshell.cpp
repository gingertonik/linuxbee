#include "shell/libraryshell.h"
#include "shell/libraryshell_p.h"

#include "actions/actionregistry.h"
#include "mainwindow/mainwindow.h"
#include "navigator/navigatorpanel.h"
#include "shell/scanrunner.h"
#include "tracklist/tracklistmodel.h"
#include "tracklist/tracklistpage.h"

#include <QAction>
#include <QHash>
#include <QList>
#include <QSet>
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

namespace {

// SH-NODE: a playlist and `playing` are shown in their own order; every other
// node is sorted by the list's keys.
bool shownInOwnOrder(const QString &nodeId)
{
    return nodeId.startsWith(kPlaylistPrefix) || nodeId == kPlayingNode;
}

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

    // SH-BUILD / SH-EXTEND: the model with its columns and sorting, the
    // page over it, and the page the window shows (id `tracks`): a
    // horizontal splitter named `browserSplitter`, holding the track list
    // alone until a browser is set.
    d->model = new TrackListModel(this);
    d->model->load(*d->settings, QStringLiteral("tracklist"));
    d->splitter = new QSplitter(Qt::Horizontal);
    d->splitter->setObjectName(QStringLiteral("browserSplitter"));
    d->page = new TrackListPage(d->model, d->actions, d->splitter);
    d->splitter->addWidget(d->page);
    d->window->addPage(QStringLiteral("tracks"), d->splitter);
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
    d->switchOrder(); // SH-NODE: the node the shell starts on
    showNode();

    // SH-LIVE: whoever changes the library or the playlists, what is on show
    // follows.
    connect(d->library, &Library::tracksAdded, this, &LibraryShell::onLibraryChanged);
    connect(d->library, &Library::tracksUpdated, this, &LibraryShell::onLibraryChanged);
    connect(d->library, &Library::tracksRemoved, this, &LibraryShell::onLibraryChanged);
    connect(d->playlists, &PlaylistStore::listChanged, this, &LibraryShell::onPlaylistsChanged);
    connect(
        d->playlists, &PlaylistStore::tracksChanged, this, &LibraryShell::onPlaylistTracksChanged);
    // SH-MOVED: a file the library says moved stays in its playlists, in its place.
    connect(d->library, &Library::pathsMoved, this,
        [this](const QHash<QString, QString> &oldToNew) { d->playlists->relink(oldToNew); });

    // SR-SHELL: the one runner. The shell connects to it here, before anyone
    // else can, so it hears every signal first.
    d->scanner = new ScanRunner(d->library, { }, this);
    connect(d->scanner, &ScanRunner::finished, this, [this](const ScanReport &report) {
        QString text = tr("Scan: %1 added, %2 updated, %3 missing, %4 not audio")
                           .arg(report.added.size())
                           .arg(report.updated.size())
                           .arg(report.nowMissing.size())
                           .arg(report.notAudio.size());
        if (report.cancelled)
            text += tr(" (stopped)");
        showMessage(text);
    });

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
    // SR-SHELL: the runner waits for its looking thread before the rest goes.
    delete d->scanner;
    d->scanner = nullptr;
    delete d;
}

ScanRunner *LibraryShell::scanner() const
{
    return d->scanner;
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
    // SH-PATHS: from the held tracks, found by id; no track is read.
    QHash<qint64, QString> pathOf;
    pathOf.reserve(d->kept.size());
    for (const LibraryTrack &track : std::as_const(d->kept))
        pathOf.insert(track.id, track.path);
    QStringList out;
    for (const qint64 id : selectedTrackIds())
        out.append(pathOf.value(id));
    return out;
}

QStringList LibraryShell::shownPaths() const
{
    // SH-PATHS: from the held tracks, found by id; no track is read.
    QHash<qint64, QString> pathOf;
    pathOf.reserve(d->kept.size());
    for (const LibraryTrack &track : std::as_const(d->kept))
        pathOf.insert(track.id, track.path);
    QStringList out;
    const int rows = d->model->rowCount();
    out.reserve(rows);
    for (int row = 0; row < rows; ++row)
        out.append(pathOf.value(d->model->trackId(row)));
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
    return d->kept; // SH-EXTEND: as last read; the library is not touched
}

void LibraryShell::Private::switchOrder()
{
    // SH-NODE: the model's given order is switched here and nowhere else: on
    // for a playlist and `playing`, off for every other node. Callers are a
    // node being chosen, the shell starting on a node, and view.refresh, each
    // before the tracks are handed over.
    model->setGivenOrder(shownInOwnOrder(node));
}

void LibraryShell::Private::showKept()
{
    // SH-NODE: shows the kept tracks and leaves the order alone, so a sort the
    // user clicked holds through a narrowing, a source's answer, the library,
    // a playlist or the queue changing.
    if (!narrowing) {
        model->setTracks(kept);
        return;
    }
    QList<LibraryTrack> shown;
    for (const LibraryTrack &track : kept) {
        if (narrowing(track))
            shown.append(track);
    }
    model->setTracks(shown);
}

void LibraryShell::setNarrowing(std::function<bool(const LibraryTrack &)> keep)
{
    // SH-EXTEND: setting or ending a narrowing reads nothing and says
    // nothing: the node's tracks did not change.
    d->narrowing = std::move(keep);
    d->showKept();
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
    d->switchOrder(); // SH-NODE: a node is chosen
    showNode();
}

void LibraryShell::showNode()
{
    // SH-EXTEND: the one place the node is read. The kept tracks are
    // replaced, the list follows, and the change is said once.
    d->kept = readNodeTracks();
    d->showKept();
    // SH-REMOVE: the Delete key is named for what it does in the view on show.
    if (QAction *remove = d->actions->action(QStringLiteral("file.remove"))) {
        if (d->node == QStringLiteral("playing"))
            remove->setText(tr("Remove from Queue"));
        else
            remove->setText(playlistOf(d->node).isEmpty() ? tr("Remove from Library")
                                                          : tr("Remove from Playlist"));
    }
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
    // SH-COUNTS: every section, how many tracks it holds. The library counts
    // them (L-COUNT); no track is read here.
    d->navigator->setLibraryCounts(d->library->categoryCounts());
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
    // SH-EXTEND: a node a source answers for is read again on every change
    // of the library. Sources are asked with a real list pointer — a source
    // that answers true has written its answer into it. The first that
    // does is the node's read (the same order readNodeTracks() uses), so its
    // answer replaces the kept tracks and the node is read once, not twice.
    for (const NodeSource &source : d->nodeSources) {
        if (!source)
            continue;
        QList<LibraryTrack> answer;
        if (source(d->node, &answer)) {
            d->kept = answer;
            d->showKept();
            emit nodeTracksChanged();
            return;
        }
    }
    if (const QString section = sectionOf(d->node); !section.isEmpty()) {
        applyLibraryDelta(ids, section);
        // SH-EXTEND: the answer of nodeTracks() may have changed with the
        // library, on any node.
        emit nodeTracksChanged();
        return;
    }
    // SH-LIVE: on a playlist node or on `playing`, the node's tracks are
    // read again. showNode() emits nodeTracksChanged.
    showNode();
}

void LibraryShell::applyLibraryDelta(const QList<qint64> &ids, const QString &section)
{
    // SH-LIVE: each track named that belongs to the section on show is added
    // or replaced in the list; each that does not (it moved to another
    // section) is taken out. The rest of the list, and the selection, are
    // left alone: upsert/remove, never setTracks.
    // SH-EXTEND: the narrowing holds through library changes — a track that
    // arrives or is updated is listed only if `keep` accepts it as it is
    // now, and a kept track that stops being kept leaves the list.
    // SH-EXTEND: the kept tracks follow the same ids, as the tracks are now
    // (added, replaced in place, or taken out); the section is not read.
    QHash<qint64, qsizetype> at;
    for (qsizetype i = 0; i < d->kept.size(); ++i)
        at.insert(d->kept.at(i).id, i);
    QSet<qint64> gone;
    QList<LibraryTrack> intoView;
    QList<qint64> outOfView;
    for (const qint64 id : ids) {
        const auto track = d->library->track(id);
        const bool inSection = track && track->category == section;
        if (inSection) {
            if (const auto it = at.constFind(id); it != at.constEnd()) {
                d->kept[it.value()] = *track;
            } else {
                d->kept.append(*track);
                at.insert(id, d->kept.size() - 1);
            }
        } else if (at.contains(id)) {
            gone.insert(id);
        }
        const bool shownNow = d->model->rowOf(id) >= 0;
        const bool keep = !d->narrowing || (track && d->narrowing(*track));
        if (inSection && keep)
            intoView.append(*track);
        else if (shownNow)
            outOfView.append(id);
    }
    if (!gone.isEmpty()) {
        d->kept.erase(std::remove_if(d->kept.begin(), d->kept.end(),
                          [&gone](const LibraryTrack &t) { return gone.contains(t.id); }),
            d->kept.end());
    }
    // SH-NODE: the order is not switched here; a track that arrives falls into
    // the sort on show.
    if (!intoView.isEmpty())
        d->model->upsertTracks(intoView);
    if (!outOfView.isEmpty())
        d->model->removeTracks(outOfView);
}

void LibraryShell::onPlaylistsChanged()
{
    // SH-LIVE: the navigator gets the new list of ids. When the playlist on
    // show is gone the navigator falls back to library:Music and announces
    // nodeSelected, which the shell follows (V-PLAYLISTS).
    d->navigator->setPlaylists(playlistIds());
    if (const QString playlist = playlistOf(d->node);
        !playlist.isEmpty() && d->node == d->navigator->currentNode())
        showNode(); // a created, renamed or removed playlist elsewhere
    refreshCounts();
}

void LibraryShell::onPlaylistTracksChanged(const QString &id)
{
    // SH-LIVE: when that playlist is on show, its tracks are read again.
    if (d->node == kPlaylistPrefix + id)
        showNode();
}

}
