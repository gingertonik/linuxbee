#include <QRegularExpression>

#include <atomic>
#include <memory>

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

// G-INFER, speed: how often a pattern was made ready since the program started.
namespace {
std::atomic<qint64> preparedCount { 0 };
}

// What is made ready from the pattern text, once, and shared between copies.
struct InferPattern::Data {
    QString problem;
    QList<Token> tokens;
    qsizetype components = 0;
    QRegularExpression expression;
};

InferPattern::InferPattern(const QString &pattern)
{
    auto data = std::make_shared<Data>();
    data->problem = parsePattern(pattern, &data->tokens);
    if (data->problem.isEmpty()) {
        data->components = componentsFor(data->tokens);
        data->expression = QRegularExpression(expressionFor(data->tokens));
        preparedCount.fetch_add(1);
    }
    d = data;
}

QString InferPattern::problem() const
{
    return d->problem;
}

InferResult InferPattern::match(const QString &path) const
{
    InferResult result;
    if (!d->problem.isEmpty())
        return result;

    QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() < d->components)
        return result;

    // The extension goes: the text from the last dot of the file name, unless
    // that dot is the name's first character.
    QString &fileName = parts.last();
    const qsizetype dot = fileName.lastIndexOf(QLatin1Char('.'));
    if (dot > 0)
        fileName.truncate(dot);

    const QStringList window = parts.mid(parts.size() - d->components);
    const QRegularExpressionMatch found = d->expression.match(window.join(QLatin1Char('/')));
    if (!found.hasMatch())
        return result;

    int group = 0;
    for (const Token &token : d->tokens) {
        if (!token.field)
            continue;
        ++group;
        const QString text = found.captured(group).trimmed();
        if (text.isEmpty())
            return InferResult();
        result.values.insert(token.text, text);
    }
    result.matched = true;
    return result;
}

// G-INFER: blank fields only when asked; no match or a bad pattern gives nothing.
TagChanges InferPattern::changes(
    const TrackTags &tags, const QString &path, bool blankFieldsOnly) const
{
    TagChanges changes;
    if (!d->problem.isEmpty())
        return changes;
    const InferResult found = match(path);
    if (!found.matched)
        return changes;

    for (auto it = found.values.cbegin(); it != found.values.cend(); ++it) {
        if (blankFieldsOnly && !tags.fields.value(it.key()).isEmpty())
            continue;
        setIfChanged(&changes, tags, it.key(), { it.value() });
    }
    return changes;
}

qint64 inferPatternsPrepared()
{
    return preparedCount.load();
}

InferResult inferFromPath(const QString &path, const QString &pattern)
{
    return InferPattern(pattern).match(path);
}

TagChanges inferTags(
    const TrackTags &tags, const QString &path, const QString &pattern, bool blankFieldsOnly)
{
    return InferPattern(pattern).changes(tags, path, blankFieldsOnly);
}

}
