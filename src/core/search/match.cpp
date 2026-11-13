#include "search.h"

#include <optional>

namespace linuxbee {

namespace {

// S-MATCH, for one term before any negation.
// A term with no field holds when its text is found inside the folded value of
// at least one search field, each field looked at on its own.
// A term with a field holds when the track knows the field and the text is
// found inside its folded value. An empty text holds when the value is not
// blank. A field the track does not know never holds.
bool termHolds(const SearchQuery::Term &term, const TagSource &track, const SearchOptions &options)
{
    if (!term.field.isEmpty()) {
        const std::optional<QString> value = track.value(term.field);
        if (!value)
            return false;
        if (term.text.isEmpty())
            return !value->trimmed().isEmpty();
        return foldForSearch(*value).contains(term.text);
    }

    for (const QString &field : options.fields) {
        const std::optional<QString> value = track.value(field);
        if (value && foldForSearch(*value).contains(term.text))
            return true;
    }
    return false;
}

}

// S-MATCH. A track matches when every group has at least one term that holds.
// A query with no groups matches every track.
bool SearchQuery::matches(const TagSource &track, const SearchOptions &options) const
{
    for (const QList<Term> &group : m_groups) {
        bool groupHolds = false;
        for (const Term &term : group) {
            // A negated term holds exactly when it would not have held.
            if (termHolds(term, track, options) != term.negated) {
                groupHolds = true;
                break;
            }
        }
        if (!groupHolds)
            return false;
    }
    return true;
}

}
