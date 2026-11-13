#pragma once
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

class SearchQuery {
public:
    // S-PARSE. Never fails: text that makes no sense as syntax is searched for
    // as it stands.
    static SearchQuery parse(const QString &text);

    bool isEmpty() const; // true: matches every track
    // S-MATCH
    bool matches(const TagSource &track, const SearchOptions &options = { }) const;
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
