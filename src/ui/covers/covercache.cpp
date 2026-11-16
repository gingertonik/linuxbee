#include "covers/covercache.h"

#include "covers/cover.h"

#include <QHash>
#include <QMetaObject>

#include <algorithm>
#include <condition_variable>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <utility>

// Contract: docs/spec/covers.md (CC-ASK, CC-KEEP, CC-THREAD).
//
// The cache's thread owns `kept` and `useOrder`. The loading thread and the
// cache's thread share `waiting`, `inflight`, `loadsBegun` and `stopping`,
// all behind `mutex`. No call of the cache opens a file: the only reader is
// run(), on the loading thread.
namespace linuxbee::ui {

namespace {

using Key = std::pair<QString, qint64>; // music file, stamp

}

struct CoverCache::Private {
    struct Entry {
        qint64 stamp = 0;
        QImage image; // null: the file has no cover
        quint64 used = 0; // its place in useOrder
    };

    CoverCache *q = nullptr;
    int edge = 64;
    CoverCacheHooks hooks;

    // Kept pictures, and the order they were used in: the first is the one
    // used longest ago, so the one to let go is found without a walk.
    QHash<QString, Entry> kept;
    std::map<quint64, QString> useOrder;
    quint64 useCount = 0;
    int limit = 512;

    int waitingLimit = 64;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::map<quint64, Key> waiting; // by the order asked: the first has waited longest
    quint64 askedCount = 0;
    std::set<Key> inflight; // asked for and not yet kept: waiting, under way or on its way back
    int loadsBegun = 0;
    bool stopping = false;
    std::thread worker;

    void use(Entry &entry, const QString &file)
    {
        useOrder.erase(entry.used);
        entry.used = ++useCount;
        useOrder.emplace(entry.used, file);
    }

    void letGoOfWhatIsOver()
    {
        while (kept.size() > limit && !useOrder.empty()) {
            const auto oldest = useOrder.begin();
            kept.remove(oldest->second);
            useOrder.erase(oldest);
        }
    }

    // Called with the mutex held.
    void dropWhatIsOver()
    {
        while (int(waiting.size()) > waitingLimit) {
            const auto oldest = waiting.begin();
            inflight.erase(oldest->second);
            waiting.erase(oldest);
        }
    }

    void ask(const Key &key)
    {
        {
            const std::lock_guard<std::mutex> lock(mutex);
            if (inflight.count(key) != 0)
                return;
            inflight.insert(key);
            waiting.emplace(++askedCount, key);
            dropWhatIsOver();
        }
        wake.notify_one();
    }

    // The loading thread: one load at a time, in the order asked.
    void run()
    {
        for (;;) {
            Key key;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [this] { return stopping || !waiting.empty(); });
                if (stopping)
                    return;
                const auto oldest = waiting.begin();
                key = oldest->second;
                waiting.erase(oldest);
                ++loadsBegun;
            }
            if (hooks.loading)
                hooks.loading(key.first);
            const QImage picture = load(key.first);
            // Queued to the cache's thread. If the cache is destroyed first,
            // its pending events go with it and this is never run.
            QMetaObject::invokeMethod(
                q, [this, key, picture] { finished(key, picture); }, Qt::QueuedConnection);
        }
    }

    QImage load(const QString &musicFile) const
    {
        const Cover found = findCover(musicFile);
        if (found.source == Cover::Source::None || found.data.isEmpty())
            return { };
        QImage picture;
        if (!picture.loadFromData(found.data) || picture.isNull())
            return { };
        if (picture.width() > edge || picture.height() > edge)
            picture = picture.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        return picture;
    }

    // On the cache's thread.
    void finished(const Key &key, const QImage &picture)
    {
        {
            const std::lock_guard<std::mutex> lock(mutex);
            if (stopping)
                return;
            inflight.erase(key);
        }
        Entry &entry = kept[key.first];
        entry.stamp = key.second;
        entry.image = picture;
        use(entry, key.first); // being kept is its first use
        letGoOfWhatIsOver();
        emit q->loaded(key.first);
    }
};

CoverCache::CoverCache(int edge, CoverCacheHooks hooks, QObject *parent)
    : QObject(parent)
    , d(new Private)
{
    d->q = this;
    d->edge = std::max(edge, 1);
    d->hooks = std::move(hooks);
    d->worker = std::thread([this] { d->run(); });
}

CoverCache::~CoverCache()
{
    {
        const std::lock_guard<std::mutex> lock(d->mutex);
        d->stopping = true;
        d->waiting.clear(); // what waits is dropped; the load under way is waited for
    }
    d->wake.notify_all();
    d->worker.join();
    delete d;
}

int CoverCache::edge() const
{
    return d->edge;
}

QImage CoverCache::cover(const QString &musicFile, qint64 stamp)
{
    const auto found = d->kept.find(musicFile);
    if (found != d->kept.end() && found->stamp == stamp) {
        d->use(*found, musicFile);
        return found->image;
    }
    d->ask({ musicFile, stamp });
    return { };
}

bool CoverCache::has(const QString &musicFile, qint64 stamp) const
{
    const auto found = d->kept.constFind(musicFile);
    return found != d->kept.constEnd() && found->stamp == stamp;
}

int CoverCache::limit() const
{
    return d->limit;
}

void CoverCache::setLimit(int entries)
{
    d->limit = std::max(entries, 1);
    d->letGoOfWhatIsOver();
}

int CoverCache::size() const
{
    return int(d->kept.size());
}

int CoverCache::waitingLimit() const
{
    return d->waitingLimit;
}

void CoverCache::setWaitingLimit(int requests)
{
    const std::lock_guard<std::mutex> lock(d->mutex);
    d->waitingLimit = std::max(requests, 1);
    d->dropWhatIsOver();
}

int CoverCache::waiting() const
{
    const std::lock_guard<std::mutex> lock(d->mutex);
    return int(d->waiting.size());
}

int CoverCache::loads() const
{
    const std::lock_guard<std::mutex> lock(d->mutex);
    return d->loadsBegun;
}

}
