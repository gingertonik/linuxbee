#include "playqueue.h"

// Q-EDIT and Q-SIGNALS. Next, Previous and repeat live in playqueue_order.cpp;
// classifyPlay lives in playcount.cpp. Spec: docs/spec/play-queue.md.
namespace linuxbee {

PlayQueue::PlayQueue(QObject *parent)
    : QObject(parent)
{
}

QList<PlayQueue::Entry> PlayQueue::entries() const
{
    return m_entries;
}

int PlayQueue::count() const
{
    return int(m_entries.size());
}

int PlayQueue::currentIndex() const
{
    return m_current;
}

QString PlayQueue::currentPath() const
{
    if (m_current < 0 || m_current >= count())
        return { };
    return m_entries.at(m_current).path;
}

// Replaces the list. A start outside the list means the first entry; an
// empty list has no current entry. Always announces the new current entry.
void PlayQueue::playNow(const QStringList &paths, int startAt)
{
    m_entries.clear();
    for (const QString &path : paths)
        m_entries.append(Entry { m_nextId++, path });
    const int n = count();
    m_current = -1;
    m_resumeAt = 0;
    if (n > 0) {
        m_current = (startAt < 0 || startAt >= n) ? 0 : startAt;
        m_resumeAt = m_current;
    }
    emit entriesChanged();
    emit currentChanged(m_current);
}

void PlayQueue::queueNext(const QStringList &paths)
{
    Q_UNUSED(paths);
}

void PlayQueue::queueLast(const QStringList &paths)
{
    Q_UNUSED(paths);
}

int PlayQueue::removeAt(const QList<int> &positions)
{
    Q_UNUSED(positions);
    return 0;
}

bool PlayQueue::move(int from, int to)
{
    Q_UNUSED(from);
    Q_UNUSED(to);
    return false;
}

void PlayQueue::clear()
{
}

bool PlayQueue::setCurrent(int index)
{
    if (index < 0 || index >= count())
        return false;
    setCurrentInternal(index);
    return true;
}

bool PlayQueue::shuffle() const
{
    return m_shuffle;
}

void PlayQueue::setShuffle(bool on, quint32 seed)
{
    Q_UNUSED(on);
    Q_UNUSED(seed);
}

void PlayQueue::reshuffle()
{
    Q_UNUSED(m_resumeAt);
    Q_UNUSED(m_nextId);
}

// Moves the current entry and remembers the place. Announces only a real change.
void PlayQueue::setCurrentInternal(int index)
{
    if (index == m_current)
        return;
    m_current = index;
    if (index >= 0)
        m_resumeAt = index;
    emit currentChanged(index);
}

}
