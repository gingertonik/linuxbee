#include "search.h"

#include "fold_p.h"

#include <QList>

#include <atomic>
#include <optional>
#include <utility>

namespace linuxbee {

namespace {

// S-CACHE: values folded for a match, in the whole process. Read from any thread.
std::atomic<qint64> foldedCount { 0 };

bool isBlank(const QString &value)
{
    for (const QChar c : value)
        if (!c.isSpace())
            return false;
    return true; // as value.trimmed().isEmpty()
}

// The names of a field list in lower case, made once for a list and not once for each
// track. One list is kept for each thread: matches over many tracks pass the same list.
struct Names {
    QStringList fields; // the list these were made for
    QStringList lower;
    QList<qsizetype> first; // for each place, the first place in the list with the same name
};

const Names &namesOf(const QStringList &fields)
{
    thread_local Names names;
    thread_local bool made = false;
    if (made && names.fields == fields)
        return names;
    names.fields = fields;
    names.lower.clear();
    names.first.clear();
    names.lower.reserve(fields.size());
    names.first.reserve(fields.size());
    for (qsizetype i = 0; i < fields.size(); ++i) {
        names.lower.append(fields.at(i).toLower());
        qsizetype first = 0;
        while (names.lower.at(first) != names.lower.at(i))
            ++first;
        names.first.append(first);
    }
    made = true;
    return names;
}

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
        entry.blank = isBlank(*value);
        entry.folded = foldForSearch(*value);
        foldedCount.fetch_add(1, std::memory_order_relaxed);
    }
    if (cache)
        cache->fields.insert(key, entry);
    return entry;
}

// S-CACHE: the joined value of the track for options.fields, made once for that list. The
// fields the cache lacks are asked for, folded in one go (their values joined by U+001F,
// one fold, cut again at the U+001F: a value's own U+001F is made a space first, so every
// U+001F there is ours), counted once each, and put into the cache.
const QString &joinedOf(const TagSource &track, const SearchOptions &options, SearchCache &cache)
{
    if (cache.joinedMade && cache.joinedFor == options.fields)
        return cache.joined;

    const Names &names = namesOf(options.fields);
    const qsizetype count = options.fields.size();

    QStringList keys; // the fields the cache lacked and the track knows, in list order
    QString raw;
    for (qsizetype i = 0; i < count; ++i) {
        if (names.first.at(i) != i)
            continue; // the same field again: its first place decides
        const QString &key = names.lower.at(i);
        if (cache.fields.contains(key))
            continue;
        const std::optional<QString> value = track.value(options.fields.at(i));
        SearchCache::Entry entry;
        if (value) {
            entry.known = true;
            entry.blank = isBlank(*value);
            if (!keys.isEmpty())
                raw.append(QChar(0x1F));
            if (value->contains(QChar(0x1F))) {
                QString plain = *value;
                plain.replace(QChar(0x1F), QLatin1Char(' '));
                raw.append(plain);
            } else {
                raw.append(*value);
            }
            keys.append(key);
        }
        cache.fields.insert(key, entry);
    }

    if (!keys.isEmpty()) {
        const QString folded = foldKeepingUnit(raw);
        const QList<QStringView> parts = QStringView(folded).split(QChar(0x1F));
        if (parts.size() == keys.size()) {
            for (qsizetype k = 0; k < keys.size(); ++k)
                cache.fields[keys.at(k)].folded = parts.at(k).toString();
        } else {
            // Not expected (a fold neither makes nor keeps a U+001F): fold each by itself.
            for (const QString &key : std::as_const(keys)) {
                const qsizetype at = names.lower.indexOf(key);
                cache.fields[key].folded
                    = foldForSearch(track.value(options.fields.at(at)).value_or(QString()));
            }
        }
        foldedCount.fetch_add(keys.size(), std::memory_order_relaxed);
    }

    QString joined;
    bool any = false;
    for (qsizetype i = 0; i < count; ++i) {
        const auto found = cache.fields.constFind(names.lower.at(i));
        if (found == cache.fields.constEnd() || !found.value().known)
            continue;
        if (any)
            joined.append(QChar(0x1F));
        joined.append(found.value().folded);
        any = true;
    }
    cache.joined = joined;
    cache.joinedFor = options.fields;
    cache.joinedMade = true;
    return cache.joined;
}

// S-MATCH, for one term before any negation.
// A term with no field holds when its text is found inside the folded value of
// at least one search field, each field looked at on its own: with a cache, in the
// joined value of the track, which gives the same answer (S-CACHE).
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

    if (cache)
        return joinedOf(track, options, *cache).contains(term.text);

    for (const QString &field : options.fields) {
        const SearchCache::Entry entry = fieldOf(field, track, nullptr);
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
