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

// Right after the current entry, or at the remembered place with none.
void PlayQueue::queueNext(const QStringList &paths)
{
    if (paths.isEmpty())
        return;
    QList<Entry> fresh;
    for (const QString &path : paths)
        fresh.append(Entry { m_nextId++, path });
    const int at = qBound(0, m_current < 0 ? m_resumeAt : m_current + 1, count());
    m_entries = m_entries.mid(0, at) + fresh + m_entries.mid(at);
    emit entriesChanged();
}

void PlayQueue::queueLast(const QStringList &paths)
{
    if (paths.isEmpty())
        return;
    for (const QString &path : paths)
        m_entries.append(Entry { m_nextId++, path });
    emit entriesChanged();
}

// Positions that do not exist are ignored; the return value counts what went.
// The current entry keeps its place in the music; if it goes, the remembered
// place becomes the entry that followed it.
int PlayQueue::removeAt(const QList<int> &positions)
{
    QList<bool> gone(count(), false);
    int removed = 0;
    for (int p : positions) {
        if (p >= 0 && p < count() && !gone.at(p)) {
            gone[p] = true;
            ++removed;
        }
    }
    if (removed == 0)
        return 0;

    auto goneBefore = [&gone](int index) {
        int total = 0;
        for (int i = 0; i < index && i < gone.size(); ++i)
            total += gone.at(i) ? 1 : 0;
        return total;
    };

    const int oldCurrent = m_current;
    int newCurrent = m_current;
    int newResume = m_resumeAt - goneBefore(m_resumeAt);
    if (m_current >= 0 && gone.at(m_current)) {
        newCurrent = -1;
        newResume = m_current - goneBefore(m_current);
    } else if (m_current >= 0) {
        newCurrent = m_current - goneBefore(m_current);
        newResume = newCurrent;
    }

    QList<Entry> kept;
    kept.reserve(count() - removed);
    for (int i = 0; i < count(); ++i) {
        if (!gone.at(i))
            kept.append(m_entries.at(i));
    }
    m_entries = kept;
    m_current = newCurrent;
    m_resumeAt = qBound(0, newResume, count());
    emit entriesChanged();
    if (m_current != oldCurrent)
        emit currentChanged(m_current);
    return removed;
}

// The entry lands at `to`. The current entry stays current, wherever it lands.
bool PlayQueue::move(int from, int to)
{
    if (from < 0 || from >= count() || to < 0 || to >= count())
        return false;
    if (from == to)
        return true;
    m_entries.move(from, to);
    int newCurrent = m_current;
    if (m_current == from)
        newCurrent = to;
    else if (from < m_current && m_current <= to)
        newCurrent = m_current - 1;
    else if (to <= m_current && m_current < from)
        newCurrent = m_current + 1;
    emit entriesChanged();
    if (newCurrent != m_current)
        setCurrentInternal(newCurrent);
    return true;
}

void PlayQueue::clear()
{
    if (m_entries.isEmpty())
        return;
    const bool hadCurrent = m_current >= 0;
    m_entries.clear();
    m_current = -1;
    m_resumeAt = 0;
    emit entriesChanged();
    if (hadCurrent)
        emit currentChanged(-1);
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
