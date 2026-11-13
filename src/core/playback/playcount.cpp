#include "playqueue.h"

// Q-COUNT: when a play counts as played, skipped or neither. Spec:
// docs/spec/play-queue.md. Defaults are in PlayCountRule (W-NP).
namespace linuxbee {

PlayOutcome classifyPlay(
    qint64 listenedMs, qint64 durationMs, bool endedByUser, const PlayCountRule &rule)
{
    const qint64 heard = listenedMs < 0 ? 0 : listenedMs;
    const bool moreThanShare
        = durationMs > 0 && double(heard) > rule.playedFraction * double(durationMs);
    const bool moreThanTime = heard > qint64(rule.playedSeconds) * 1000;
    if (moreThanShare || moreThanTime)
        return PlayOutcome::Played;
    if (endedByUser && heard < qint64(rule.skipSeconds) * 1000)
        return PlayOutcome::Skipped;
    return PlayOutcome::Neither;
}

}
