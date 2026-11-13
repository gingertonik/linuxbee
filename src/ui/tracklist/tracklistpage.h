#pragma once
#include <QList>
#include <QWidget>

class QLineEdit;
class QTreeView;

// A track list on screen: a search box, the list, and a summary line.
// Contract: docs/spec/track-list.md (T-PAGE).
namespace linuxbee::ui {

class ActionRegistry;
class TrackListModel;

class TrackListPage : public QWidget {
    Q_OBJECT
public:
    // Neither pointer is owned; both must outlive the page.
    TrackListPage(TrackListModel *model, ActionRegistry *actions, QWidget *parent = nullptr);
    ~TrackListPage() override;

    QList<qint64> selectedTrackIds() const; // in the order shown
    void selectTracks(const QList<qint64> &ids);
    QString searchText() const;
    void setSearchText(const QString &text);

signals:
    void selectionChanged();
    // The user double-clicked a track or pressed Return on it.
    void trackActivated(qint64 trackId);

private:
    struct Private;
    Private *d;
};

}
