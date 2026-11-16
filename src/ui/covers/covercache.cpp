#include "covers/covercache.h"

// Stub from the seed: the builder replaces this file. Contract: docs/spec/covers.md (CC-x).
namespace linuxbee::ui {

struct CoverCache::Private {
    int edge = 64;
};

CoverCache::CoverCache(int edge, CoverCacheHooks, QObject *parent)
    : QObject(parent)
    , d(new Private { edge })
{
}

CoverCache::~CoverCache()
{
    delete d;
}

int CoverCache::edge() const
{
    return d->edge;
}

QImage CoverCache::cover(const QString &, qint64)
{
    return { };
}

bool CoverCache::has(const QString &, qint64) const
{
    return false;
}

int CoverCache::limit() const
{
    return 0;
}

void CoverCache::setLimit(int)
{
}

int CoverCache::size() const
{
    return 0;
}

int CoverCache::waitingLimit() const
{
    return 0;
}

void CoverCache::setWaitingLimit(int)
{
}

int CoverCache::waiting() const
{
    return 0;
}

int CoverCache::loads() const
{
    return 0;
}

}
