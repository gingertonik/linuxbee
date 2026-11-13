#include "playqueue.h"

// Q-NEXT: Next and Previous in list order, and repeat. Shuffle joins in task 4.
// Spec: docs/spec/play-queue.md.
namespace linuxbee {

// Next, from the current entry or, with none, from the place the queue remembers.
int PlayQueue::next(bool automatic)
{
    const int n = count();
    if (n == 0)
        return -1;
    if (m_current >= 0 && automatic && m_repeat == Repeat::One)
        return m_current;
    int target = m_current < 0 ? m_resumeAt : m_current + 1;
    if (target >= n)
        target = m_repeat == Repeat::All ? 0 : -1;
    if (target < 0)
        return -1;
    setCurrentInternal(target);
    return target;
}

int PlayQueue::previous()
{
    const int n = count();
    if (n == 0)
        return -1;
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
}

}
