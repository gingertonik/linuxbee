#include <QRegularExpression>

#include "common.h"
#include "tagtools.h"

namespace linuxbee {

namespace {

using tagtools::canonicalField;
using tagtools::setIfChanged;

// One piece of a pattern: literal text, or a catalogue field name.
struct Token {
    bool field = false;
    QString text;
};

// G-INFER: these fields take digits only.
bool isNumberField(const QString &field)
{
    return field == QLatin1String("Track#") || field == QLatin1String("Track Count")
        || field == QLatin1String("Disc#") || field == QLatin1String("Disc Count")
        || field == QLatin1String("Year");
}

// Splits a pattern into tokens. Returns what is wrong with it, or an empty text.
QString parsePattern(const QString &pattern, QList<Token> *tokens)
{
    if (pattern.isEmpty())
        return QStringLiteral("the pattern is empty");

    QStringList seen;
    QString literal;
    qsizetype i = 0;
    while (i < pattern.size()) {
        if (pattern.at(i) != QLatin1Char('<')) {
            literal += pattern.at(i);
            ++i;
            continue;
        }
        const qsizetype close = pattern.indexOf(QLatin1Char('>'), i + 1);
        if (close < 0)
            return QStringLiteral("a \"<\" is not closed");
        const QString name = pattern.mid(i + 1, close - i - 1);
        const QString field = canonicalField(name);
        if (field.isEmpty())
            return QStringLiteral("no field named \"%1\"").arg(name);
        if (seen.contains(field))
            return QStringLiteral("field \"%1\" is named twice").arg(field);
        if (!literal.isEmpty()) {
            tokens->append(Token { false, literal });
            literal.clear();
        } else if (!tokens->isEmpty() && tokens->last().field) {
            return QStringLiteral("two fields with nothing between them, at \"%1\"").arg(name);
        }
        tokens->append(Token { true, field });
        seen.append(field);
        i = close + 1;
    }
    if (!literal.isEmpty())
        tokens->append(Token { false, literal });
    if (seen.isEmpty())
        return QStringLiteral("the pattern names no field");
    return { };
}

// The expression that must match the last folders and the file name, whole.
// A text field takes as few characters as it can and never a "/"; a number
// field takes digits.
QString expressionFor(const QList<Token> &tokens)
{
    QString expression = QStringLiteral("\\A");
    for (const Token &token : tokens) {
        if (!token.field)
            expression += QRegularExpression::escape(token.text);
        else if (isNumberField(token.text))
            expression += QStringLiteral("([0-9]+)");
        else
            expression += QStringLiteral("([^/]+?)");
    }
    return expression + QStringLiteral("\\z");
}

// One component for the file name, plus one for each "/" in the pattern.
qsizetype componentsFor(const QList<Token> &tokens)
{
    qsizetype count = 1;
    for (const Token &token : tokens) {
        if (!token.field)
            count += token.text.count(QLatin1Char('/'));
    }
    return count;
}

}

QString validateInferPattern(const QString &pattern)
{
    QList<Token> tokens;
    return parsePattern(pattern, &tokens);
}

InferResult inferFromPath(const QString &path, const QString &pattern)
{
    InferResult result;
    QList<Token> tokens;
    if (!parsePattern(pattern, &tokens).isEmpty())
        return result;

    QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const qsizetype components = componentsFor(tokens);
    if (parts.size() < components)
        return result;

    // The extension goes: the text from the last dot of the file name, unless
    // that dot is the name's first character.
    QString &fileName = parts.last();
    const qsizetype dot = fileName.lastIndexOf(QLatin1Char('.'));
    if (dot > 0)
        fileName.truncate(dot);

    const QStringList window = parts.mid(parts.size() - components);
    const QRegularExpression expression(expressionFor(tokens));
    const QRegularExpressionMatch match = expression.match(window.join(QLatin1Char('/')));
    if (!match.hasMatch())
        return result;

    int group = 0;
    for (const Token &token : tokens) {
        if (!token.field)
            continue;
        ++group;
        const QString text = match.captured(group).trimmed();
        if (text.isEmpty())
            return InferResult();
        result.values.insert(token.text, text);
    }
    result.matched = true;
    return result;
}

// Stub from the seed: the builder replaces this block. It answers rightly and counts nothing.
struct InferPattern::Data {
    QString pattern;
};

InferPattern::InferPattern(const QString &pattern)
    : d(std::make_shared<const Data>(Data { pattern }))
{
}

QString InferPattern::problem() const
{
    return validateInferPattern(d->pattern);
}

InferResult InferPattern::match(const QString &path) const
{
    return inferFromPath(path, d->pattern);
}

TagChanges InferPattern::changes(const TrackTags &tags, const QString &path, bool blankFieldsOnly) const
{
    return inferTags(tags, path, d->pattern, blankFieldsOnly);
}

qint64 inferPatternsPrepared()
{
    return 0;
}

// G-INFER: blank fields only when asked; no match or a bad pattern gives nothing.
TagChanges inferTags(
    const TrackTags &tags, const QString &path, const QString &pattern, bool blankFieldsOnly)
{
    TagChanges changes;
    if (!validateInferPattern(pattern).isEmpty())
        return changes;
    const InferResult found = inferFromPath(path, pattern);
    if (!found.matched)
        return changes;

    for (auto it = found.values.cbegin(); it != found.values.cend(); ++it) {
        if (blankFieldsOnly && !tags.fields.value(it.key()).isEmpty())
            continue;
        setIfChanged(&changes, tags, it.key(), { it.value() });
    }
    return changes;
}

}
