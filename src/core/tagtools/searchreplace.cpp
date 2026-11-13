#include <QRegularExpression>

#include "common.h"
#include "tagtools.h"

namespace linuxbee {

namespace {

using tagtools::canonicalField;
using tagtools::setIfChanged;

// G-REPLACE: the expression a search runs with. Plain text is escaped; whole
// word wraps the expression in letter-or-digit lookarounds.
QString searchPattern(const SearchReplaceOptions &options)
{
    const QString body = options.regex ? options.find : QRegularExpression::escape(options.find);
    if (!options.wholeWord)
        return body;
    return QStringLiteral("(?<![\\p{L}\\p{N}])(?:%1)(?![\\p{L}\\p{N}])").arg(body);
}

QRegularExpression::PatternOptions searchOptions(const SearchReplaceOptions &options)
{
    QRegularExpression::PatternOptions flags = QRegularExpression::UseUnicodePropertiesOption;
    if (!options.matchCase)
        flags |= QRegularExpression::CaseInsensitiveOption;
    return flags;
}

// G-REPLACE: $1 to $9 are the groups (a group that took no part is empty),
// $$ is a dollar sign, and any other $ is itself. A group number the
// expression does not have is also itself.
QString expandGroups(const QRegularExpressionMatch &match, const QString &replacement)
{
    const int groups = match.regularExpression().captureCount();
    QString out;
    for (qsizetype i = 0; i < replacement.size(); ++i) {
        const QChar ch = replacement.at(i);
        if (ch != QLatin1Char('$') || i + 1 >= replacement.size()) {
            out += ch;
            continue;
        }
        const QChar next = replacement.at(i + 1);
        if (next == QLatin1Char('$')) {
            out += QLatin1Char('$');
            ++i;
            continue;
        }
        if (next >= QLatin1Char('1') && next <= QLatin1Char('9')) {
            const int group = next.unicode() - '0';
            if (group <= groups) {
                out += match.captured(group);
                ++i;
                continue;
            }
        }
        out += ch;
    }
    return out;
}

QString replaceIn(
    const QString &value, const QRegularExpression &expression, const SearchReplaceOptions &options)
{
    QString out;
    qsizetype last = 0;
    QRegularExpressionMatchIterator matches = expression.globalMatch(value);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        out += value.mid(last, match.capturedStart() - last);
        out += options.regex ? expandGroups(match, options.replace) : options.replace;
        last = match.capturedEnd();
    }
    out += value.mid(last);
    return out;
}

// The fields a search covers: those named, or every catalogue field but Rating.
QStringList searchedFields(const SearchReplaceOptions &options)
{
    QStringList fields;
    if (options.fields.isEmpty()) {
        for (const QString &name : tagFieldNames()) {
            if (name != QLatin1String("Rating"))
                fields.append(name);
        }
    } else {
        for (const QString &name : options.fields)
            fields.append(canonicalField(name));
    }
    return fields;
}

}

QString validateSearchReplace(const SearchReplaceOptions &options)
{
    if (options.find.isEmpty())
        return QStringLiteral("find is empty");
    for (const QString &name : options.fields) {
        if (canonicalField(name).isEmpty())
            return QStringLiteral("no field named \"%1\"").arg(name);
    }
    const QRegularExpression expression(searchPattern(options), searchOptions(options));
    if (!expression.isValid())
        return expression.errorString();
    return { };
}

TagChanges searchReplace(const TrackTags &tags, const SearchReplaceOptions &options)
{
    TagChanges changes;
    if (!validateSearchReplace(options).isEmpty())
        return changes;

    // Built once per call, not once per value.
    const QRegularExpression expression(searchPattern(options), searchOptions(options));
    for (const QString &field : searchedFields(options)) {
        const QStringList current = tags.fields.value(field);
        if (current.isEmpty())
            continue;
        QStringList replaced;
        for (const QString &value : current)
            replaced.append(replaceIn(value, expression, options));
        setIfChanged(&changes, tags, field, replaced);
    }
    return changes;
}

}
