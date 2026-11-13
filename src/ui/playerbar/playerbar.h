#pragma once
#include <QString>
#include <QWidget>

#include "playback/player.h"
#include "playback/playqueue.h"

class QSettings;

// The player controls at the bottom of the window: transport buttons, the
// seek bar with times, the track's name, volume and mute. It also gives the
// Controls menu its behaviour. Contract: docs/spec/player-bar.md (PB-x).
namespace linuxbee::ui {

class ActionRegistry;

class PlayerBar : public QWidget {
    Q_OBJECT
public:
    // None of the three is owned; all must outlive the bar.
    PlayerBar(Player *player, PlayQueue *queue, ActionRegistry *actions, QWidget *parent = nullptr);
    ~PlayerBar() override;

    // PB-TITLE: what to show for the track playing now, e.g. "Artist - Title".
    // Lasts until the track changes.
    void setTrackText(const QString &text);

    // PB-TIME
    static QString formatTime(qint64 ms);

    // PB-STORE
    void saveState(QSettings &settings) const;
    void restoreState(QSettings &settings);

private:
    struct Private;
    Private *d;
};

}
