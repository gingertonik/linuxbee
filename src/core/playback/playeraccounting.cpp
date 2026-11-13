#include "playback/player_p.h"

// X-LISTEN and X-COUNT: the time heard of a play, and when a play is counted
// as played, skipped or neither. Contract: docs/spec/player.md. The
// classification itself is classifyPlay (playcount.cpp, Q-COUNT).
namespace linuxbee {

// X-LISTEN: the time heard is the sum of the forward steps between reports
// while Playing. A report while not playing, or one that goes backwards,
// adds nothing but becomes the new starting point. A seek moves the starting
// point too (it is the position, see Private::seek).
void Player::Private::onBackendPosition(qint64 ms)
{
    if (state == State::Stopped)
        return;
    if (state == State::Playing && ms > position)
        heardMs += ms - position;
    setPosition(ms);
}

// X-LISTEN: the file reported its end, so the stretch from the last report to
// the duration counts as heard, when the duration is known.
void Player::Private::countStretchToEnd()
{
    if (state == State::Playing && duration > position)
        heardMs += duration - position;
}

// X-COUNT: a track is accounted for once, when it stops being the track
// played. endedByUser is false only when the file ran to its end by itself.
void Player::Private::accountCurrent(bool endedByUser)
{
    if (accounted || entryId == 0)
        return;
    accounted = true;
    switch (classifyPlay(heardMs, duration, endedByUser, countRule)) {
    case PlayOutcome::Played:
        emit q->playCounted(path);
        break;
    case PlayOutcome::Skipped:
        emit q->skipCounted(path);
        break;
    case PlayOutcome::Neither:
        break;
    }
}

}
