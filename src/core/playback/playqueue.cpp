#include "playqueue.h"

// Stub. Plan 14 replaces every body in this file.
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
    return 0;
}

int PlayQueue::currentIndex() const
{
    return m_current;
}

QString PlayQueue::currentPath() const
{
    return { };
}

void PlayQueue::playNow(const QStringList &paths, int startAt)
{
    Q_UNUSED(paths);
    Q_UNUSED(startAt);
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
    Q_UNUSED(index);
    return false;
}

int PlayQueue::next(bool automatic)
{
    Q_UNUSED(automatic);
    return -1;
}

int PlayQueue::previous()
{
    return -1;
}

bool PlayQueue::hasNext(bool automatic) const
{
    Q_UNUSED(automatic);
    return false;
}

PlayQueue::Repeat PlayQueue::repeat() const
{
    return m_repeat;
}

void PlayQueue::setRepeat(Repeat repeat)
{
    Q_UNUSED(repeat);
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

void PlayQueue::setCurrentInternal(int index)
{
    Q_UNUSED(index);
}

PlayOutcome classifyPlay(
    qint64 listenedMs, qint64 durationMs, bool endedByUser, const PlayCountRule &rule)
{
    Q_UNUSED(listenedMs);
    Q_UNUSED(durationMs);
    Q_UNUSED(endedByUser);
    Q_UNUSED(rule);
    return PlayOutcome::Neither;
}

}
