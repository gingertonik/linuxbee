#include "shell/pendingplays.h"

#include "library/library.h"

#include <QtGlobal>

#include <deque>

// PS-COUNT (docs/spec/playback-shell.md): a play or a skip the library refused is kept, by the
// path of its file and the time it happened, and offered again with the next one, oldest first.
namespace linuxbee::ui {

struct PendingPlays::Private {
    PendingPlays *owner = nullptr;
    linuxbee::Library *library = nullptr;
    // Oldest first. What is kept is what Library::recordPlays gave back.
    std::deque<linuxbee::PlayRecord> kept;
    bool droppedSaid = false;

    // All of what is kept, in one call; what the library gives back is what is kept now.
    void offerKept()
    {
        if (kept.empty())
            return;
        QList<linuxbee::PlayRecord> records;
        records.reserve(static_cast<qsizetype>(kept.size()));
        for (const linuxbee::PlayRecord &record : kept)
            records.append(record);
        const QList<linuxbee::PlayRecord> back = library->recordPlays(records);
        kept.assign(back.cbegin(), back.cend());
    }

    // No more than kMost are kept: the oldest goes, and that is said the first time since nothing
    // was kept.
    void bound()
    {
        while (static_cast<int>(kept.size()) > kMost) {
            kept.pop_front();
            if (!droppedSaid) {
                droppedSaid = true;
                emit owner->oldestDropped();
            }
        }
    }

    void add(const linuxbee::PlayRecord &record)
    {
        const int before = static_cast<int>(kept.size());
        kept.push_back(record); // joins what is kept, and all of it is offered at once
        offerKept();
        bound();
        said(before);
    }

    void said(int before)
    {
        const int now = static_cast<int>(kept.size());
        if (now == 0)
            droppedSaid = false;
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
}

PendingPlays::~PendingPlays()
{
    // PS-COUNT: what is still kept is lost, and that is said once.
    const qsizetype lost = static_cast<qsizetype>(d->kept.size());
    if (lost == 0)
        return;
    const QString what = lost == 1 ? QStringLiteral("1 play or skip")
                                   : QString::number(lost) + QStringLiteral(" plays or skips");
    qWarning().noquote() << QStringLiteral("linuxBee: ") + what
            + QStringLiteral(" could not be counted before closing, because the library could "
                             "not be changed; they are lost");
}

void PendingPlays::play(const QString &path, const QDateTime &when)
{
    d->add({ path, when, false });
}

void PendingPlays::skip(const QString &path, const QDateTime &when)
{
    d->add({ path, when, true });
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
