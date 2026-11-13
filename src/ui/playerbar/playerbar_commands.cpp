#include "playerbar/playerbar_p.h"

#include "actions/actionregistry.h"

#include <QAction>

namespace linuxbee::ui {

// PB-COMMANDS: the controls.* commands. The bar gives each one its behaviour
// while it exists; the registry takes that back when the bar is destroyed.
void PlayerBar::implementCommands()
{
    ActionRegistry *actions = d->actions;
    Player *player = d->player;
    QAction *mute = actions->action(QStringLiteral("controls.mute"));

    actions->implement(
        QStringLiteral("controls.playPause"), this, [player] { player->playPause(); });
    actions->implement(QStringLiteral("controls.stop"), this, [player] { player->stop(); });
    actions->implement(QStringLiteral("controls.next"), this, [player] { player->next(); });
    actions->implement(QStringLiteral("controls.previous"), this, [player] { player->previous(); });
    // A tick-box: the registry runs this after Qt has flipped the tick, so read isChecked().
    actions->implement(QStringLiteral("controls.mute"), this,
        [player, mute] { player->setMuted(mute->isChecked()); });
    // Task 5 gives these their behaviour; until then they do nothing.
    actions->implement(QStringLiteral("controls.stopAfterCurrent"), this, [] { });
    actions->implement(QStringLiteral("controls.volumeUp"), this, [] { });
    actions->implement(QStringLiteral("controls.volumeDown"), this, [] { });
    actions->implement(QStringLiteral("controls.repeat"), this, [] { });
    actions->implement(QStringLiteral("controls.shuffle"), this, [] { });
}

}
