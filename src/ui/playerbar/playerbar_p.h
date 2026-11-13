#pragma once
// The shared guts of PlayerBar: playerbar.cpp builds the frame and shows the
// player's state, playerbar_commands.cpp gives the controls.* commands their
// behaviour (docs/spec/player-bar.md, PB-COMMANDS).
#include "playerbar/playerbar.h"

class QLabel;
class QSlider;
class QToolButton;

namespace linuxbee::ui {

struct PlayerBar::Private {
    Player *player = nullptr;
    PlayQueue *queue = nullptr;
    ActionRegistry *actions = nullptr;
    qint64 durationMs = 0; // PB-STATE: 0 until the length is known
    QLabel *titleLabel = nullptr;
    QToolButton *previousButton = nullptr;
    QToolButton *playPauseButton = nullptr;
    QToolButton *stopButton = nullptr;
    QToolButton *nextButton = nullptr;
    QToolButton *muteButton = nullptr;
    QLabel *positionLabel = nullptr;
    QSlider *seekSlider = nullptr;
    QLabel *durationLabel = nullptr;
    QSlider *volumeSlider = nullptr;
};

} // namespace linuxbee::ui
