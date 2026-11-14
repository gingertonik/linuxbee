#include "shell/albumviewing.h"

#include <QBoxLayout>
#include <QComboBox>
#include <QHash>
#include <QMenu>
#include <QPointer>
#include <QScopedValueRollback>
#include <QSet>
#include <QSettings>
#include <QSignalBlocker>
#include <QTreeView>

#include "albumview/albumtracksview.h"
#include "library/albumgroups.h"
#include "shell/libraryshell.h"
#include "tracklist/tracklistmodel.h"
#include "tracklist/tracklistpage.h"

// Contract: docs/spec/album-view.md (AV-SHELL). This object owns no rule: the
// view says what was clicked, and the library shell is asked to do the rest.
namespace linuxbee::ui {

namespace {

QString albumsMode()
{
    return QStringLiteral("albums");
}

QString tracksMode()
{
    return QStringLiteral("tracks");
}

// AV-SHELL: `tracks` when the text is that (any case); anything else is `albums`.
QString modeOf(const QString &text)
{
    return text.compare(tracksMode(), Qt::CaseInsensitive) == 0 ? tracksMode() : albumsMode();
}

}

struct AlbumViewing::Private {
    AlbumViewing *q = nullptr;
    LibraryShell *shell = nullptr;
    QSettings *settings = nullptr;
    TrackListPage *page = nullptr;
    QPointer<QTreeView> table; // trackView: the plain list
    QPointer<AlbumTracksView> view;
    QPointer<QComboBox> combo; // mainViewCombo
    QString mode;
    // AV-FILL: true while the view is behind what the list holds. A change
    // only sets it; one fill, at the next turn of the event loop or when the
    // view is asked for, makes the view current.
    bool stale = false;
    bool queued = false; // the call that fills a stale view is already on its way
    // True while the selection is carried from one side to the other, so that
    // the signal of the side that was written to is not read back as a choice.
    bool syncing = false;

    bool inAlbums() const { return mode == albumsMode(); }
    // AV-SHELL: shows what the mode asks for, and gives the view its tracks.
    void apply();
    // AV-SHELL: the view gets the tracks the list holds, in the order of the
    // view, or none in mode `tracks`. At once; this is the one place that reads.
    void refill();
    // AV-FILL: a change of the model or of the node's tracks: the view is
    // marked, and one fill is asked for at the next turn of the event loop.
    void markStale();
    // AV-FILL: fills the view if it is stale, and does nothing if it is not.
    void bringUpToDate();
    // AV-SHELL, the list's selection to the view's: the view is brought up to
    // date, and selects what the list has selected unless it already has it.
    void followList();
    // AV-SHELL, the view's selection to the list's.
    void followView();
    // AV-SHELL: the shell is asked to play, as if the list had been used.
    void playTrack(qint64 id);
    void playAlbum(int group);
};

void AlbumViewing::Private::apply()
{
    const bool albums = inAlbums();
    {
        const QSignalBlocker blocker(combo.data());
        combo->setCurrentIndex(albums ? 0 : 1);
    }
    if (table)
        table->setVisible(!albums);
    view->setVisible(albums);
    // A change of mode fills or empties at once, and in mode `albums` the view
    // then selects what the list has selected.
    refill();
    followList();
}

void AlbumViewing::Private::refill()
{
    if (view.isNull())
        return;
    stale = false; // however it comes about, the view is not behind after this
    if (!inAlbums()) {
        if (!view->groups().isEmpty())
            view->setTracks({ });
        return;
    }
    // The rows of the list say which tracks and how many; the shell's kept
    // tracks say what they are.
    const QList<LibraryTrack> node = shell->nodeTracks();
    QHash<qint64, qsizetype> at;
    at.reserve(node.size());
    for (qsizetype i = 0; i < node.size(); ++i)
        at.insert(node.at(i).id, i);
    const TrackListModel *model = shell->model();
    QList<LibraryTrack> shown;
    shown.reserve(model->rowCount());
    for (int row = 0; row < model->rowCount(); ++row) {
        const auto found = at.constFind(model->trackId(row));
        if (found != at.cend())
            shown.append(node.at(found.value()));
    }
    view->setTracks(sortedForAlbums(shown));
}

void AlbumViewing::Private::markStale()
{
    // In mode `tracks` the view is empty and nothing makes it stale.
    if (!inAlbums() || view.isNull())
        return;
    stale = true;
    if (queued)
        return; // a fill is already asked for: one for many signals
    queued = true;
    // The context is the owner, so the call is dropped if it is destroyed first.
    QMetaObject::invokeMethod(
        q,
        [this] {
            queued = false;
            bringUpToDate();
        },
        Qt::QueuedConnection);
}

void AlbumViewing::Private::bringUpToDate()
{
    if (stale)
        refill();
}

void AlbumViewing::Private::followList()
{
    if (syncing || !inAlbums() || view.isNull())
        return;
    // The fill below can change the view's selection (tracks that are gone);
    // that is not the user's choice, and the list's selection is the one to keep.
    const QScopedValueRollback<bool> guard(syncing, true);
    bringUpToDate();
    const QList<qint64> wanted = page->selectedTrackIds();
    const QList<qint64> shown = view->selectedTrackIds();
    // Compared as sets: a selected heading stands for its tracks, and when the
    // view has exactly the list's tracks nothing is touched.
    if (QSet<qint64>(wanted.begin(), wanted.end()) != QSet<qint64>(shown.begin(), shown.end()))
        view->selectTracks(wanted);
}

void AlbumViewing::Private::followView()
{
    if (syncing || !inAlbums() || view.isNull())
        return;
    const QScopedValueRollback<bool> guard(syncing, true);
    page->selectTracks(view->selectedTrackIds());
}

void AlbumViewing::Private::playTrack(qint64 id)
{
    if (!inAlbums() || view.isNull())
        return;
    // Everything the view shows, in its order, from that track.
    QStringList paths;
    qsizetype index = -1;
    for (const AlbumGroup &group : view->groups()) {
        const qsizetype in = group.trackIds.indexOf(id);
        if (in >= 0 && index < 0)
            index = paths.size() + in;
        paths += group.paths;
    }
    if (index < 0)
        return; // not shown: nothing is asked
    emit shell->playRequested(paths, int(index));
}

void AlbumViewing::Private::playAlbum(int group)
{
    if (!inAlbums() || view.isNull())
        return;
    const QList<AlbumGroup> groups = view->groups();
    if (group < 0 || group >= groups.size())
        return;
    emit shell->playRequested(groups.at(group).paths, 0);
}

AlbumViewing::AlbumViewing(LibraryShell *shell, QSettings *settings, QObject *parent)
    : QObject(parent)
    , d(new Private)
{
    d->q = this;
    d->shell = shell;
    d->settings = settings;
    d->page = shell->trackList();
    d->table = d->page->findChild<QTreeView *>(QStringLiteral("trackView"));

    // AV-SHELL: the view and the combo box go into the page: the combo box at
    // the top, the view directly after the plain list.
    d->view = new AlbumTracksView(d->page);
    d->combo = new QComboBox(d->page);
    d->combo->setObjectName(QStringLiteral("mainViewCombo"));
    d->combo->addItem(tr("Album and Tracks"));
    d->combo->addItem(tr("Tracks"));
    if (auto *layout = qobject_cast<QBoxLayout *>(d->page->layout())) {
        layout->insertWidget(0, d->combo);
        layout->insertWidget(layout->indexOf(d->table.data()) + 1, d->view, 1);
    }

    connect(d->combo, &QComboBox::currentIndexChanged, this,
        [this](int index) { setMode(index == 1 ? tracksMode() : albumsMode()); });

    // AV-SHELL, while in mode `albums`: the view's selection is the shell's,
    // and the other way round: the list's selection is the view's.
    connect(d->view, &AlbumTracksView::selectionChanged, this, [this] { d->followView(); });
    connect(d->page, &TrackListPage::selectionChanged, this, [this] { d->followList(); });
    connect(
        d->view, &AlbumTracksView::trackActivated, this, [this](qint64 id) { d->playTrack(id); });
    connect(d->view, &AlbumTracksView::albumActivated, this,
        [this](int group) { d->playAlbum(group); });
    connect(d->view, &AlbumTracksView::menuRequested, this, [this](const QPoint &pos) {
        if (!d->inAlbums())
            return;
        if (auto *menu = d->page->findChild<QMenu *>(QStringLiteral("trackContextMenu")))
            menu->popup(pos);
    });

    // AV-FILL: every change of what the list holds, and of the node, only marks
    // the view as stale; it is filled once, at the next turn of the event loop.
    const auto follow = [this] { d->markStale(); };
    const TrackListModel *model = shell->model();
    connect(model, &QAbstractItemModel::modelReset, this, follow);
    connect(model, &QAbstractItemModel::rowsInserted, this, follow);
    connect(model, &QAbstractItemModel::rowsRemoved, this, follow);
    connect(model, &QAbstractItemModel::rowsMoved, this, follow);
    connect(model, &QAbstractItemModel::layoutChanged, this, follow);
    connect(model, &QAbstractItemModel::dataChanged, this, follow);
    connect(shell, &LibraryShell::nodeTracksChanged, this, follow);

    // The mode is read as text, as an earlier run stored it.
    d->mode = modeOf(settings->value(QStringLiteral("view/mainPanel")).toString());
    d->apply();
}

AlbumViewing::~AlbumViewing()
{
    // The widgets were put into the page of a shell that may be gone by now:
    // what is still there is taken out, and the plain list is shown again.
    delete d->view.data();
    delete d->combo.data();
    if (d->table)
        d->table->setVisible(true);
    delete d;
}

AlbumTracksView *AlbumViewing::view() const
{
    // AV-FILL: a stale view is filled before it is handed out.
    d->bringUpToDate();
    return d->view.data();
}

QString AlbumViewing::mode() const
{
    return d->mode;
}

void AlbumViewing::setMode(const QString &mode)
{
    const QString next = modeOf(mode);
    d->settings->setValue(QStringLiteral("view/mainPanel"), next);
    if (next == d->mode)
        return;
    d->mode = next;
    d->apply();
    emit modeChanged();
}

}
