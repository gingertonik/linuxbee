#include "search.h"

namespace linuxbee {

namespace {

// One token as the text cut it. Outside quotes decide the field split and the
// operator; inside quotes, white space and colons are kept as they are.
struct Token {
    bool negated = false;
    bool quoted = false; // a double quote was opened in the token
    bool hasField = false;
    QString field; // raw field name, quotes removed
    QString text; // raw text, quotes removed
};

// Reads the token that starts at pos. The caller has skipped white space, so
// the token has at least one character, and pos is left after it.
// S-PARSE: white space ends a token except inside double quotes. A leading
// "-" followed by anything but white space negates the token and is dropped.
// The first colon outside quotes that has something before it splits off the
// field name; later colons belong to the text.
Token readToken(const QString &text, qsizetype &pos)
{
    Token token;
    const qsizetype size = text.size();

    if (text.at(pos) == QLatin1Char('-') && pos + 1 < size && !text.at(pos + 1).isSpace()) {
        token.negated = true;
        ++pos;
    }

    QString body;
    qsizetype colon = -1; // index in body of the field colon, if there is one
    bool inQuotes = false;
    while (pos < size) {
        const QChar c = text.at(pos);
        if (c.isSpace() && !inQuotes)
            break;
        ++pos;
        if (c == QLatin1Char('"')) {
            inQuotes = !inQuotes;
            token.quoted = true;
            continue;
        }
        if (c == QLatin1Char(':') && !inQuotes && colon < 0 && !body.isEmpty())
            colon = body.size();
        body.append(c);
    }

    if (colon >= 0) {
        token.hasField = true;
        token.field = body.left(colon);
        token.text = body.mid(colon + 1);
    } else {
        token.text = body;
    }
    return token;
}

// S-CANONICAL: a field name holding white space is written in double quotes.
QString writeFieldName(const QString &name)
{
    for (const QChar c : name) {
        if (c.isSpace())
            return QStringLiteral("\"") + name + QStringLiteral("\"");
    }
    return name;
}

}

// S-PARSE. Never fails: every token either becomes a term or is dropped.
SearchQuery SearchQuery::parse(const QString &text)
{
    SearchQuery query;
    bool orPending = false; // an OR is waiting for the term after it
    qsizetype pos = 0;
    while (true) {
        while (pos < text.size() && text.at(pos).isSpace())
            ++pos;
        if (pos >= text.size())
            break;

        const Token token = readToken(text, pos);

        // The operator is exactly OR, in upper case, unquoted, with no field and
        // no dash. It joins the term before it and the term after it.
        if (!token.quoted && !token.negated && !token.hasField
            && token.text == QStringLiteral("OR")) {
            // With no term before it, or a second OR in a row, there is nothing
            // to join, so it is dropped.
            if (!query.m_groups.isEmpty() && !orPending)
                orPending = true;
            continue;
        }

        Term term;
        term.negated = token.negated;
        term.field = foldForSearch(token.field); // empty when the token has no field
        term.text = foldForSearch(token.text);
        // A token with an empty text and no field is dropped. A token with a
        // field and an empty text (genre:) is kept.
        if (!token.hasField && term.text.isEmpty())
            continue;

        if (orPending && !query.m_groups.isEmpty()) {
            query.m_groups.last().append(term);
        } else {
            query.m_groups.append(QList<Term> { term });
        }
        orPending = false;
    }
    return query;
}

bool SearchQuery::isEmpty() const
{
    return m_groups.isEmpty();
}

// S-CANONICAL: each term as its text in double quotes, with field: before it
// (the name quoted when it holds white space) and - before that when negated;
// the terms of a group joined by " OR "; the groups joined by one space.
QString SearchQuery::canonical() const
{
    QStringList groups;
    for (const QList<Term> &group : m_groups) {
        QStringList terms;
        for (const Term &term : group) {
            QString out = term.negated ? QStringLiteral("-") : QString();
            if (!term.field.isEmpty())
                out += writeFieldName(term.field) + QStringLiteral(":");
            out += QStringLiteral("\"") + term.text + QStringLiteral("\"");
            terms.append(out);
        }
        groups.append(terms.join(QStringLiteral(" OR ")));
    }
    return groups.join(QStringLiteral(" "));
}

}
