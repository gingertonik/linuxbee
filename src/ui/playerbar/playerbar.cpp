#include "playerbar/playerbar.h"

// Stub. Plan 14e replaces every body in this file.
namespace linuxbee::ui {

struct PlayerBar::Private { };

PlayerBar::PlayerBar(Player *player, PlayQueue *queue, ActionRegistry *actions, QWidget *parent)
    : QWidget(parent)
    , d(nullptr)
{
    Q_UNUSED(player);
    Q_UNUSED(queue);
    Q_UNUSED(actions);
}

PlayerBar::~PlayerBar()
{
    Q_UNUSED(d);
}

void PlayerBar::setTrackText(const QString &text)
{
    Q_UNUSED(text);
}

QString PlayerBar::formatTime(qint64 ms)
{
    Q_UNUSED(ms);
    return { };
}

void PlayerBar::saveState(QSettings &settings) const
{
    Q_UNUSED(settings);
}

void PlayerBar::restoreState(QSettings &settings)
{
    Q_UNUSED(settings);
}

}
