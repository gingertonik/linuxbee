#include "tracklist/tracklistpage.h"

// Stub. Plan 11 replaces every body in this file.
namespace linuxbee::ui {

struct TrackListPage::Private { };

TrackListPage::TrackListPage(TrackListModel *model, ActionRegistry *actions, QWidget *parent)
    : QWidget(parent)
    , d(nullptr)
{
    Q_UNUSED(model);
    Q_UNUSED(actions);
}

TrackListPage::~TrackListPage()
{
    Q_UNUSED(d);
}

QList<qint64> TrackListPage::selectedTrackIds() const
{
    return { };
}

void TrackListPage::selectTracks(const QList<qint64> &ids)
{
    Q_UNUSED(ids);
}

QString TrackListPage::searchText() const
{
    return { };
}

void TrackListPage::setSearchText(const QString &text)
{
    Q_UNUSED(text);
}

}
