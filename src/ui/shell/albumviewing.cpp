#include "shell/albumviewing.h"

#include <QBoxLayout>
#include <QComboBox>
#include <QHash>
#include <QMenu>
#include <QPointer>
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
    LibraryShell *shell = nullptr;
    QSettings *settings = nullptr;
    TrackListPage *page = nullptr;
    QPointer<QTreeView> table; // trackView: the plain list
    QPointer<AlbumTracksView> view;
    QPointer<QComboBox> combo; // mainViewCombo
    QString mode;

    bool inAlbums() const { return mode == albumsMode(); }
    // AV-SHELL: shows what the mode asks for, and gives the view its tracks.
    void apply();
    // AV-SHELL: the view gets the tracks the list holds, in the order of the
    // view, or none in mode `tracks`.
    void refill();
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
    refill();
}

void AlbumViewing::Private::refill()
{
    if (view.isNull())
        return;
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

    // AV-SHELL, while in mode `albums`: the view's selection is the shell's.
    connect(d->view, &AlbumTracksView::selectionChanged, this, [this] {
        if (d->inAlbums())
            d->page->selectTracks(d->view->selectedTrackIds());
    });
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

    // The view follows every change of what the list holds, and of the node.
    const auto follow = [this] { d->refill(); };
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
