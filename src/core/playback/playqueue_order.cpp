#include "playqueue.h"

#include <utility>

// Q-NEXT: Next and Previous in list order, or along the shuffle round, and repeat.
// Q-SHUFFLE: the round (m_upcoming) and the way back (m_history), both held as
// entry ids so that they stay with their entries. Spec: docs/spec/play-queue.md.
namespace linuxbee {

namespace {

// Where the entry with this id sits in the list, or -1 when it is not there.
int indexOfId(const QList<PlayQueue::Entry> &entries, qint64 id)
{
    for (int i = 0; i < entries.size(); ++i) {
        if (entries.at(i).entryId == id)
            return i;
    }
    return -1;
}

}

// Next, from the current entry or, with none, from the place the queue remembers.
int PlayQueue::next(bool automatic)
{
    const int n = count();
    if (n == 0)
        return -1;
    if (m_current >= 0 && automatic && m_repeat == Repeat::One)
        return m_current;

    if (m_shuffle) {
        if (m_upcoming.isEmpty()) {
            if (m_repeat != Repeat::All)
                return -1;
            reshuffle();
            if (m_upcoming.isEmpty())
                return m_current; // one entry under Repeat::All: it stays current
        }
        const int target = indexOfId(m_entries, m_upcoming.takeFirst());
        if (target < 0)
            return -1;
        if (m_current >= 0)
            m_history.append(m_entries.at(m_current).entryId);
        setCurrentInternal(target);
        return target;
    }

    int target = m_current < 0 ? m_resumeAt : m_current + 1;
    if (target >= n)
        target = m_repeat == Repeat::All ? 0 : -1;
    if (target < 0)
        return -1;
    setCurrentInternal(target);
    return target;
}

// Q-PEEK: next() without the moving, branch for branch. A round used up under Repeat::All is
// dealt here on a copy of the random sequence, so the queue's own is not drawn from.
std::optional<PlayQueue::Entry> PlayQueue::peekNext(bool automatic) const
{
    const int n = count();
    if (n == 0)
        return std::nullopt;
    if (m_current >= 0 && automatic && m_repeat == Repeat::One)
        return m_entries.at(m_current);

    if (m_shuffle) {
        qint64 id = 0;
        if (!m_upcoming.isEmpty()) {
            id = m_upcoming.first();
        } else {
            if (m_repeat != Repeat::All)
                return std::nullopt;
            std::mt19937 copy = m_random;
            const QList<qint64> round = dealRound(copy);
            if (round.isEmpty())
                return m_current >= 0 ? std::optional<Entry>(m_entries.at(m_current))
                                      : std::nullopt;
            id = round.first();
        }
        const int target = indexOfId(m_entries, id);
        if (target < 0)
            return std::nullopt;
        return m_entries.at(target);
    }

    int target = m_current < 0 ? m_resumeAt : m_current + 1;
    if (target >= n)
        target = m_repeat == Repeat::All ? 0 : -1;
    if (target < 0)
        return std::nullopt;
    return m_entries.at(target);
}

// Previous. Under shuffle it goes back through the way back and never wraps.
int PlayQueue::previous()
{
    const int n = count();
    if (n == 0)
        return -1;

    if (m_shuffle) {
        if (m_history.isEmpty())
            return -1;
        const int target = indexOfId(m_entries, m_history.takeLast());
        if (target < 0)
            return -1;
        if (m_current >= 0)
            m_upcoming.prepend(m_entries.at(m_current).entryId);
        setCurrentInternal(target);
        return target;
    }

    int target = m_current < 0 ? m_resumeAt - 1 : m_current - 1;
    if (target < 0)
        target = m_repeat == Repeat::All ? n - 1 : -1;
    if (target < 0)
        return -1;
    setCurrentInternal(target);
    return target;
}

bool PlayQueue::hasNext(bool automatic) const
{
    const int n = count();
    if (n == 0)
        return false;
    if (m_current >= 0 && automatic && m_repeat == Repeat::One)
        return true;
    if (m_shuffle)
        return !m_upcoming.isEmpty() || m_repeat == Repeat::All;
    const int from = m_current < 0 ? m_resumeAt : m_current + 1;
    return from < n || m_repeat == Repeat::All;
}

PlayQueue::Repeat PlayQueue::repeat() const
{
    return m_repeat;
}

void PlayQueue::setRepeat(Repeat repeat)
{
    if (repeat == m_repeat)
        return;
    m_repeat = repeat;
    emit modeChanged();
    emit nextChanged();
}

// Switching shuffle on starts a round and an empty way back. Switching it on
// again starts a new round with the new seed and keeps the way back. Switching
// it off drops both and leaves Next in list order.
void PlayQueue::setShuffle(bool on, quint32 seed)
{
    if (on) {
        if (!m_shuffle) {
            m_shuffle = true;
            m_history.clear();
            emit modeChanged();
        }
        m_random.seed(seed);
        reshuffle();
        emit nextChanged(); // a new round was dealt, and no other signal tells of it
        return;
    }
    if (!m_shuffle)
        return;
    m_shuffle = false;
    m_upcoming.clear();
    m_history.clear();
    emit modeChanged();
    emit nextChanged();
}

// A new round: every entry except the current one, in the order the seed's
// random sequence gives (Fisher-Yates; m_random % n is the same on every platform).
void PlayQueue::reshuffle()
{
    m_upcoming = dealRound(m_random);
}

// The dealing itself, on the random sequence it is given: reshuffle() hands it the queue's own,
// peekNext() a copy, so that looking leaves the sequence as it was.
QList<qint64> PlayQueue::dealRound(std::mt19937 &random) const
{
    const qint64 current = m_current >= 0 ? m_entries.at(m_current).entryId : qint64(-1);
    QList<qint64> round;
    for (const Entry &entry : std::as_const(m_entries)) {
        if (entry.entryId != current)
            round.append(entry.entryId);
    }
    for (int i = round.size() - 1; i > 0; --i) {
        const int j = int(random() % quint32(i + 1));
        std::swap(round[i], round[j]);
    }
    return round;
}

}
