#include "search.h"

#include <atomic>
#include <optional>

namespace linuxbee {

namespace {

// S-CACHE: values folded for a match, in the whole process. Read from any thread.
std::atomic<qint64> foldedCount { 0 };

// One field of one track, as a match needs it (S-CACHE). From the cache when
// it has the field, by the name in lower case; else by asking the track,
// folding (counted) and putting the answer into the cache. A field the track
// does not know is remembered too and is not counted. Without a cache nothing
// is remembered.
SearchCache::Entry fieldOf(const QString &field, const TagSource &track, SearchCache *cache)
{
    const QString key = field.toLower();
    if (cache) {
        const auto found = cache->fields.constFind(key);
        if (found != cache->fields.constEnd())
            return found.value();
    }
    SearchCache::Entry entry;
    const std::optional<QString> value = track.value(field);
    if (value) {
        entry.known = true;
        entry.blank = value->trimmed().isEmpty();
        entry.folded = foldForSearch(*value);
        foldedCount.fetch_add(1, std::memory_order_relaxed);
    }
    if (cache)
        cache->fields.insert(key, entry);
    return entry;
}

// S-MATCH, for one term before any negation.
// A term with no field holds when its text is found inside the folded value of
// at least one search field, each field looked at on its own.
// A term with a field holds when the track knows the field and the text is
// found inside its folded value. An empty text holds when the value is not
// blank. A field the track does not know never holds.
bool termHolds(const SearchQuery::Term &term, const TagSource &track, const SearchOptions &options,
    SearchCache *cache)
{
    if (!term.field.isEmpty()) {
        const SearchCache::Entry entry = fieldOf(term.field, track, cache);
        if (!entry.known)
            return false;
        if (term.text.isEmpty())
            return !entry.blank;
        return entry.folded.contains(term.text);
    }

    for (const QString &field : options.fields) {
        const SearchCache::Entry entry = fieldOf(field, track, cache);
        if (entry.known && entry.folded.contains(term.text))
            return true;
    }
    return false;
}

}

// S-MATCH. A track matches when every group has at least one term that holds.
// A query with no groups matches every track.
bool SearchQuery::matches(const TagSource &track, const SearchOptions &options) const
{
    return matches(track, options, nullptr);
}

// S-CACHE: the same answer; fields the cache has are not folded again.
bool SearchQuery::matches(
    const TagSource &track, const SearchOptions &options, SearchCache *cache) const
{
    for (const QList<Term> &group : m_groups) {
        bool groupHolds = false;
        for (const Term &term : group) {
            // A negated term holds exactly when it would not have held.
            if (termHolds(term, track, options, cache) != term.negated) {
                groupHolds = true;
                break;
            }
        }
        if (!groupHolds)
            return false;
    }
    return true;
}

qint64 searchValuesFolded()
{
    return foldedCount.load(std::memory_order_relaxed);
}

}
