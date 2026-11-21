#pragma once
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include "template/template.h"

// The search box: what the user types, and which tracks it finds.
// Contract: docs/spec/search.md. Nothing here reads the disk.
namespace linuxbee {

// S-FOLD: lower case, accents removed. "Beyoncé" and "BEYONCE" fold alike.
QString foldForSearch(const QString &text);

// S-FIELDS: the fields a plain word is looked for in.
QStringList defaultSearchFields();

struct SearchOptions {
    QStringList fields = defaultSearchFields();
};

// S-CACHE: what one track's fields fold to, remembered between matches.
// Empty when made; the caller keeps one for each track.
struct SearchCache {
    struct Entry {
        bool known = false; // the track knows the field
        bool blank = true; // its value, trimmed, is empty
        QString folded;
    };
    QHash<QString, Entry> fields; // by the field's name in lower case
    // For a term with no field: the folded values of the search fields the track knows,
    // in the order of joinedFor, each from the next by U+001F. Made once for that list.
    bool joinedMade = false;
    QStringList joinedFor;
    QString joined;
};

// S-CACHE: values folded for a match, in the whole process, since it started.
qint64 searchValuesFolded();

// S-CACHE: makes the track's joined value for options.fields, as a term with no field would.
void prepareSearch(const TagSource &track, const SearchOptions &options, SearchCache *cache);

class SearchQuery {
public:
    // S-PARSE. Never fails: text that makes no sense as syntax is searched for
    // as it stands.
    static SearchQuery parse(const QString &text);

    bool isEmpty() const; // true: matches every track
    // S-MATCH
    bool matches(const TagSource &track, const SearchOptions &options = { }) const;
    // S-CACHE: the same answer; fields the cache has are not folded again.
    bool matches(const TagSource &track, const SearchOptions &options, SearchCache *cache) const;
    // S-CANONICAL: the query written out in full, for showing and testing.
    QString canonical() const;

    struct Term {
        QString field; // folded; empty: any of SearchOptions::fields
        QString text; // folded
        bool negated = false;
    };
    // A track matches when, in every group, at least one term matches.
    const QList<QList<Term>> &groups() const { return m_groups; }

private:
    QList<QList<Term>> m_groups;
};

}
