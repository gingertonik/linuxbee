#include "search/search.h"

// STUB (review-filter-fast). The builder gives these real bodies in
// match.cpp and removes this file.
namespace linuxbee {

qint64 searchValuesFolded()
{
    return 0;
}

bool SearchQuery::matches(const TagSource &track, const SearchOptions &options, SearchCache *) const
{
    return matches(track, options);
}

}
