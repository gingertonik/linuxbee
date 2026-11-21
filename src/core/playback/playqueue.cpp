#include "playqueue.h"

#include <QSet>

// Q-EDIT and Q-SIGNALS. Next, Previous, repeat and shuffle live in
// playqueue_order.cpp; classifyPlay lives in playcount.cpp.
// Spec: docs/spec/play-queue.md.
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
    // A new list is a new round, and the way back belonged to the old list.
    m_history.clear();
    m_upcoming.clear();
    m_waiting.clear();
    if (m_shuffle)
        reshuffle();
    emit entriesChanged();
    emit currentChanged(m_current);
    emit nextChanged();
}

// Right after the current entry, or at the remembered place with none, and
// after the earlier queueNext tracks still waiting there. Under shuffle the
// tracks are played next, after the waiting ones, in the order given.
void PlayQueue::queueNext(const QStringList &paths)
{
    if (paths.isEmpty())
        return;
    QList<Entry> fresh;
    for (const QString &path : paths)
        fresh.append(Entry { m_nextId++, path });
    int at = qBound(0, m_current < 0 ? m_resumeAt : m_current + 1, count());
    while (at < count() && m_waiting.contains(m_entries.at(at).entryId))
        ++at;
    m_entries = m_entries.mid(0, at) + fresh + m_entries.mid(at);
    QList<qint64> ids;
    for (const Entry &entry : fresh) {
        ids.append(entry.entryId);
        m_waiting.insert(entry.entryId);
    }
    if (m_shuffle) {
        int front = 0;
        while (front < m_upcoming.size() && m_waiting.contains(m_upcoming.at(front)))
            ++front;
        m_upcoming = m_upcoming.mid(0, front) + ids + m_upcoming.mid(front);
    }
    emit entriesChanged();
    emit nextChanged();
}

// At the end of the list. Under shuffle each track joins the round at a place
// the seed's random sequence decides.
void PlayQueue::queueLast(const QStringList &paths)
{
    if (paths.isEmpty())
        return;
    for (const QString &path : paths) {
        const qint64 id = m_nextId++;
        m_entries.append(Entry { id, path });
        if (m_shuffle) {
            const int at = int(m_random() % quint32(m_upcoming.size() + 1));
            m_upcoming.insert(at, id);
        }
    }
    emit entriesChanged();
    emit nextChanged();
}

// Positions that do not exist are ignored; the return value counts what went.
// The current entry keeps its place in the music; if it goes, the remembered
// place becomes the entry that followed it. Removed entries leave the round
// and the way back.
int PlayQueue::removePaths(const QStringList &paths)
{
    // One pass over the list with a set of the paths; one removeAt for all, so one change.
    const QSet<QString> wanted(paths.cbegin(), paths.cend());
    QList<int> positions;
    for (int i = 0; i < count(); ++i) {
        if (i != m_current && wanted.contains(m_entries.at(i).path))
            positions.append(i);
    }
    if (positions.isEmpty())
        return 0;
    return removeAt(positions);
}

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

    QSet<qint64> dropped;
    QList<Entry> kept;
    kept.reserve(count() - removed);
    for (int i = 0; i < count(); ++i) {
        if (gone.at(i)) {
            dropped.insert(m_entries.at(i).entryId);
            m_waiting.remove(m_entries.at(i).entryId);
        } else {
            kept.append(m_entries.at(i));
        }
    }
    m_entries = kept;
    QList<qint64> round;
    for (qint64 id : m_upcoming) {
        if (!dropped.contains(id))
            round.append(id);
    }
    m_upcoming = round;
    QList<qint64> back;
    for (qint64 id : m_history) {
        if (!dropped.contains(id))
            back.append(id);
    }
    m_history = back;

    m_current = newCurrent;
    m_resumeAt = qBound(0, newResume, count());
    emit entriesChanged();
    if (m_current != oldCurrent)
        emit currentChanged(m_current);
    emit nextChanged();
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
        setCurrentInternal(newCurrent); // says nextChanged itself
    else
        emit nextChanged();
    return true;
}

// Empties the list and the round and the way back. Shuffle, if on, stays on.
void PlayQueue::clear()
{
    if (m_entries.isEmpty())
        return;
    const bool hadCurrent = m_current >= 0;
    m_entries.clear();
    m_upcoming.clear();
    m_history.clear();
    m_waiting.clear();
    m_current = -1;
    m_resumeAt = 0;
    emit entriesChanged();
    if (hadCurrent)
        emit currentChanged(-1);
    emit nextChanged();
}

// A jump under shuffle: the entry left joins the way back, and the entry
// jumped to is not played again in this round.
bool PlayQueue::setCurrent(int index)
{
    if (index < 0 || index >= count())
        return false;
    if (index == m_current)
        return true;
    if (m_shuffle) {
        if (m_current >= 0)
            m_history.append(m_entries.at(m_current).entryId);
        m_upcoming.removeAll(m_entries.at(index).entryId);
    }
    setCurrentInternal(index);
    return true;
}

bool PlayQueue::shuffle() const
{
    return m_shuffle;
}

// Moves the current entry and remembers the place. Announces only a real change.
void PlayQueue::setCurrentInternal(int index)
{
    if (index == m_current)
        return;
    m_current = index;
    if (index >= 0) {
        m_resumeAt = index;
        m_waiting.remove(m_entries.at(index).entryId); // Q-EDIT: it has become current
    }
    emit currentChanged(index);
    emit nextChanged();
}

// Q-RENAME: each entry is looked up once, where it is. Ids, order, the current
// entry and the shuffle lists are about entry ids, so they stay as they are.
int PlayQueue::renamePaths(const QHash<QString, QString> &oldToNew)
{
    int changed = 0;
    for (Entry &entry : m_entries) {
        const auto found = oldToNew.constFind(entry.path);
        if (found == oldToNew.constEnd() || found.value() == entry.path)
            continue;
        entry.path = found.value();
        ++changed;
    }
    if (changed > 0) {
        emit entriesChanged();
        emit nextChanged();
    }
    return changed;
}

}
