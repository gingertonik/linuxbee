#include "functions.h"

#include "evaluator.h"
#include "node.h"
#include "template.h"

#include <QLocale>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <array>
#include <optional>

namespace linuxbee::tmpl {
namespace {

// The evaluated text of one argument (T-QUOTE).
QString argText(const std::vector<Argument> &arguments, size_t index, const EvalContext &context)
{
    return evaluateSequence(arguments[index].left, context, false);
}

QString callUpper(const std::vector<Argument> &arguments, const EvalContext &context)
{
    return argText(arguments, 0, context).toUpper();
}

QString callLower(const std::vector<Argument> &arguments, const EvalContext &context)
{
    return argText(arguments, 0, context).toLower();
}

QString callTrim(const std::vector<Argument> &arguments, const EvalContext &context)
{
    return argText(arguments, 0, context).trimmed();
}

QString callLen(const std::vector<Argument> &arguments, const EvalContext &context)
{
    // T-RANGE: characters are counted as UTF-16 code units, as QString counts them.
    return QString::number(argText(arguments, 0, context).size());
}

QString callLeft(const std::vector<Argument> &arguments, const EvalContext &context)
{
    const QString x = argText(arguments, 0, context);
    const std::optional<int> count = toCount(argText(arguments, 1, context));
    if (!count.has_value())
        return { }; // T-RANGE
    return x.left(*count);
}

QString callRight(const std::vector<Argument> &arguments, const EvalContext &context)
{
    const QString x = argText(arguments, 0, context);
    const std::optional<int> count = toCount(argText(arguments, 1, context));
    if (!count.has_value())
        return { }; // T-RANGE
    return x.right(*count);
}

QString callCutLeft(const std::vector<Argument> &arguments, const EvalContext &context)
{
    const QString x = argText(arguments, 0, context);
    const std::optional<int> count = toCount(argText(arguments, 1, context));
    if (!count.has_value())
        return { }; // T-RANGE
    return x.mid(*count);
}

QString callCutRight(const std::vector<Argument> &arguments, const EvalContext &context)
{
    const QString x = argText(arguments, 0, context);
    const std::optional<int> count = toCount(argText(arguments, 1, context));
    if (!count.has_value())
        return { }; // T-RANGE
    // A count at or past the length leaves nothing (T-RANGE). QString::left with a
    // negative length returns the whole string, so the bound is checked here.
    if (*count >= x.size())
        return { };
    return x.left(x.size() - *count);
}

QString callPad(const std::vector<Argument> &arguments, const EvalContext &context)
{
    const QString x = argText(arguments, 0, context);
    const std::optional<int> count = toCount(argText(arguments, 1, context));
    if (!count.has_value())
        return x; // T-RANGE: $Pad returns x unchanged
    return x.rightJustified(*count, QLatin1Char('0'));
}

QString callFirst(const std::vector<Argument> &arguments, const EvalContext &context)
{
    const QString x = argText(arguments, 0, context);
    const QString separator = context.options.multiValueSeparator;
    if (separator.isEmpty())
        return x;
    const qsizetype at = x.indexOf(separator);
    if (at < 0)
        return x;
    return x.left(at);
}

struct RomanPair {
    int value;
    QLatin1StringView numeral;
};

// The standard subtractive table (plan 01, task 3; MATH in the test file).
constexpr std::array<RomanPair, 13> kRomans { {
    { 1000, QLatin1StringView("M") },
    { 900, QLatin1StringView("CM") },
    { 500, QLatin1StringView("D") },
    { 400, QLatin1StringView("CD") },
    { 100, QLatin1StringView("C") },
    { 90, QLatin1StringView("XC") },
    { 50, QLatin1StringView("L") },
    { 40, QLatin1StringView("XL") },
    { 10, QLatin1StringView("X") },
    { 9, QLatin1StringView("IX") },
    { 5, QLatin1StringView("V") },
    { 4, QLatin1StringView("IV") },
    { 1, QLatin1StringView("I") },
} };

QString callRomanNumerals(const std::vector<Argument> &arguments, const EvalContext &context)
{
    const QString x = argText(arguments, 0, context);
    const std::optional<int> value = toCount(x);
    if (!value.has_value() || *value < 1 || *value > 3999)
        return x; // T-RANGE
    QString out;
    int rest = *value;
    for (const RomanPair &pair : kRomans) {
        while (rest >= pair.value) {
            out += pair.numeral;
            rest -= pair.value;
        }
    }
    return out;
}

// The table, in the order of the spec's section 4 rows. Tasks 4 to 7 add the
// rest of the rows.
constexpr std::array<FunctionSpec, 11> kFunctions { {
    { QLatin1StringView("Upper"), 1, 0, &callUpper },
    { QLatin1StringView("Lower"), 1, 0, &callLower },
    { QLatin1StringView("Trim"), 1, 0, &callTrim },
    { QLatin1StringView("Len"), 1, 0, &callLen },
    { QLatin1StringView("Left"), 2, 0, &callLeft },
    { QLatin1StringView("Right"), 2, 0, &callRight },
    { QLatin1StringView("CutLeft"), 2, 0, &callCutLeft },
    { QLatin1StringView("CutRight"), 2, 0, &callCutRight },
    { QLatin1StringView("Pad"), 2, 0, &callPad },
    { QLatin1StringView("First"), 1, 0, &callFirst },
    { QLatin1StringView("RomanNumerals"), 1, 0, &callRomanNumerals },
} };

} // namespace

int findFunction(QStringView name)
{
    for (size_t index = 0; index < kFunctions.size(); ++index) {
        if (name.compare(kFunctions[index].name, Qt::CaseInsensitive) == 0)
            return int(index);
    }
    return -1;
}

const FunctionSpec &functionAt(int index)
{
    // Only ever called with an index from findFunction, or with one the parser
    // stored from findFunction.
    return kFunctions[size_t(index)];
}

std::optional<int> toCount(const QString &text)
{
    // T-RANGE: a whole number, 0 or more. Built once; QRegularExpression is
    // reentrant when used read-only.
    static const QRegularExpression kPattern(QStringLiteral("^\\s*\\d+\\s*$"));
    if (!kPattern.match(text).hasMatch())
        return std::nullopt;
    bool ok = false;
    const int value = QLocale::c().toInt(text.trimmed(), &ok);
    if (!ok)
        return std::nullopt;
    return value;
}

} // namespace linuxbee::tmpl
