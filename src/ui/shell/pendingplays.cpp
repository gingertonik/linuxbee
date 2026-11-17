#include "shell/pendingplays.h"

#include "library/library.h"

#include <deque>

// PS-COUNT (docs/spec/playback-shell.md): a play or a skip the library refused is kept, with the
// time it happened, and offered again oldest first.
namespace linuxbee::ui {

struct PendingPlays::Private {
    struct Entry {
        qint64 id = 0;
        QDateTime when;
        bool skip = false;
    };

    PendingPlays *owner = nullptr;
    linuxbee::Library *library = nullptr;
    std::deque<Entry> kept;
    bool droppedSaid = false;
    // True while this object's own call to the library runs: only a refusal said then is ours.
    bool asking = false;
    bool refused = false;

    // The library's answer to one entry: true when it was counted or had nothing to do for it
    // (a track it does not have), false when it refused (it said `failed` during the call).
    bool offer(const Entry &entry)
    {
        asking = true;
        refused = false;
        if (entry.skip)
            library->recordSkip(entry.id, entry.when);
        else
            library->recordPlay(entry.id, entry.when);
        asking = false;
        return !refused;
    }

    // Oldest first; stops at the first one the library refuses.
    void offerKept()
    {
        while (!kept.empty()) {
            if (!offer(kept.front()))
                return;
            kept.pop_front();
        }
    }

    void keep(const Entry &entry)
    {
        if (static_cast<int>(kept.size()) >= kMost) {
            kept.pop_front();
            if (!droppedSaid) {
                droppedSaid = true;
                emit owner->oldestDropped();
            }
        }
        kept.push_back(entry);
    }

    void add(const Entry &entry)
    {
        const int before = static_cast<int>(kept.size());
        offerKept();
        if (!kept.empty())
            keep(entry); // still refused: this one joins untried
        else if (!offer(entry))
            keep(entry);
        said(before);
    }

    void said(int before)
    {
        const int now = static_cast<int>(kept.size());
        if (now != before)
            emit owner->keptChanged(now);
    }
};

PendingPlays::PendingPlays(linuxbee::Library *library, QObject *parent)
    : QObject(parent)
    , d(new Private)
{
    d->owner = this;
    d->library = library;
    connect(library, &linuxbee::Library::failed, this, [this] {
        if (d->asking)
            d->refused = true;
    });
}

PendingPlays::~PendingPlays() = default;

void PendingPlays::play(qint64 trackId, const QDateTime &when)
{
    d->add({ trackId, when, false });
}

void PendingPlays::skip(qint64 trackId, const QDateTime &when)
{
    d->add({ trackId, when, true });
}

void PendingPlays::retry()
{
    const int before = static_cast<int>(d->kept.size());
    d->offerKept();
    d->said(before);
}

int PendingPlays::kept() const
{
    return static_cast<int>(d->kept.size());
}

}
