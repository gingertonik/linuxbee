#include "search.h"

// Stub. Plan 20 replaces every body in this file.
namespace linuxbee {

SearchQuery SearchQuery::parse(const QString &text)
{
    Q_UNUSED(text);
    return { };
}

bool SearchQuery::isEmpty() const
{
    return false;
}

bool SearchQuery::matches(const TagSource &track, const SearchOptions &options) const
{
    Q_UNUSED(track);
    Q_UNUSED(options);
    return false;
}

QString SearchQuery::canonical() const
{
    return { };
}

}
