#include "functions.h"

#include "evaluator.h"
#include "node.h"
#include "template.h"

#include <QLocale>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cmath>
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

// T-COMPARE: `$And`, `$Or`, `$Not`, `$Contains` and `$IsMatch` return the
// one-letter texts T and F.
QString boolean(bool value)
{
    return value ? QStringLiteral("T") : QStringLiteral("F");
}

QString callIsNull(const std::vector<Argument> &arguments, const EvalContext &context)
{
    // W-F: "a when x is empty, otherwise b". "0" is not empty.
    const QString x = argText(arguments, 0, context);
    return x.isEmpty() ? argText(arguments, 1, context) : argText(arguments, 2, context);
}

QString callIf(const std::vector<Argument> &arguments, const EvalContext &context)
{
    return evaluateCondition(arguments[0], context) ? argText(arguments, 1, context)
                                                    : argText(arguments, 2, context);
}

QString callAnd(const std::vector<Argument> &arguments, const EvalContext &context)
{
    return boolean(
        evaluateCondition(arguments[0], context) && evaluateCondition(arguments[1], context));
}

QString callOr(const std::vector<Argument> &arguments, const EvalContext &context)
{
    return boolean(
        evaluateCondition(arguments[0], context) || evaluateCondition(arguments[1], context));
}

QString callNot(const std::vector<Argument> &arguments, const EvalContext &context)
{
    return boolean(!evaluateCondition(arguments[0], context));
}

QString callContains(const std::vector<Argument> &arguments, const EvalContext &context)
{
    // T-CASE: $Contains ignores case.
    return boolean(argText(arguments, 0, context)
            .contains(argText(arguments, 1, context), Qt::CaseInsensitive));
}

QString callIsMatch(const std::vector<Argument> &arguments, const EvalContext &context)
{
    const QRegularExpression pattern(argText(arguments, 1, context));
    if (!pattern.isValid())
        return boolean(false); // T-REGEX: a pattern that does not compile never matches
    return boolean(pattern.match(argText(arguments, 0, context)).hasMatch());
}

// T-RANGE: the index of a split is a count from 1; 0, a value beyond the
// number of sections, or a count that is not a whole number gives an empty
// string.
QString pickSection(const QStringList &sections, const QString &indexText)
{
    const std::optional<int> index = toCount(indexText);
    if (!index.has_value() || *index < 1 || *index > sections.size())
        return { };
    return sections[*index - 1];
}

QString callReplace(const std::vector<Argument> &arguments, const EvalContext &context)
{
    const QString x = argText(arguments, 0, context);
    const QString from = argText(arguments, 1, context);
    if (from.isEmpty())
        return x; // T-RANGE
    QString out = x;
    out.replace(from, argText(arguments, 2, context), Qt::CaseSensitive); // T-CASE
    return out;
}

// T-REGEX: in the `to` text, '$' followed by a digit 0 to 9 is the whole match
// or that capture group; every other character is literal.
QString expandGroups(const QString &replacement, const QRegularExpressionMatch &match)
{
    QString out;
    for (qsizetype index = 0; index < replacement.size(); ++index) {
        const QChar ch = replacement.at(index);
        const QChar next = index + 1 < replacement.size() ? replacement.at(index + 1) : QChar();
        if (ch == u'$' && next >= u'0' && next <= u'9') {
            out += match.captured(next.digitValue());
            ++index;
            continue;
        }
        out += ch;
    }
    return out;
}

QString callRxReplace(const std::vector<Argument> &arguments, const EvalContext &context)
{
    const QString x = argText(arguments, 0, context);
    const QRegularExpression pattern(argText(arguments, 1, context));
    if (!pattern.isValid())
        return x; // T-REGEX
    const QString replacement = argText(arguments, 2, context);
    // The replacement is built by walking the matches: QString::replace with a
    // QRegularExpression writes group references as "\1", not "$1".
    QString out;
    qsizetype doneTo = 0;
    QRegularExpressionMatchIterator matches = pattern.globalMatch(x);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        out += x.mid(doneTo, match.capturedStart() - doneTo);
        out += expandGroups(replacement, match);
        doneTo = match.capturedStart() + match.capturedLength();
    }
    out += x.mid(doneTo);
    return out;
}

QString callRxMatch(const std::vector<Argument> &arguments, const EvalContext &context)
{
    const QRegularExpression pattern(argText(arguments, 1, context));
    if (!pattern.isValid())
        return { }; // T-REGEX
    const QRegularExpressionMatch match = pattern.match(argText(arguments, 0, context));
    if (!match.hasMatch())
        return { }; // T-RANGE
    return match.captured(0);
}

QString callSplit(const std::vector<Argument> &arguments, const EvalContext &context)
{
    const QString x = argText(arguments, 0, context);
    const QString separator = argText(arguments, 1, context);
    if (separator.isEmpty())
        return pickSection({ x }, argText(arguments, 2, context)); // T-RANGE: do not split
    // T-CASE: $Split is case-sensitive.
    return pickSection(
        x.split(separator, Qt::KeepEmptyParts, Qt::CaseSensitive), argText(arguments, 2, context));
}

QString callRSplit(const std::vector<Argument> &arguments, const EvalContext &context)
{
    const QString x = argText(arguments, 0, context);
    const QString separator = argText(arguments, 1, context);
    QStringList sections = separator.isEmpty()
        ? QStringList { x }
        : x.split(separator, Qt::KeepEmptyParts, Qt::CaseSensitive);
    std::reverse(sections.begin(), sections.end()); // W-F: counting from the right
    return pickSection(sections, argText(arguments, 2, context));
}

QString callRxSplit(const std::vector<Argument> &arguments, const EvalContext &context)
{
    const QString x = argText(arguments, 0, context);
    const QRegularExpression pattern(argText(arguments, 1, context));
    if (!pattern.isValid())
        return pickSection({ x }, argText(arguments, 2, context)); // T-REGEX: x is one section
    return pickSection(x.split(pattern, Qt::KeepEmptyParts), argText(arguments, 2, context));
}

// T-MATH: arithmetic on the two argument numbers. The operator is passed so
// the five rows of the spec's table share one implementation.
QString arithmetic(const std::vector<Argument> &arguments, const EvalContext &context, QChar op)
{
    const std::optional<double> a = toNumber(argText(arguments, 0, context));
    const std::optional<double> b = toNumber(argText(arguments, 1, context));
    if (!a.has_value() || !b.has_value())
        return { }; // T-MATH: an input that is not a number
    if ((op == u'/' || op == u'%') && *b == 0.0)
        return { }; // T-MATH: division or modulo by zero
    switch (op.unicode()) {
    case u'+':
        return formatNumber(*a + *b);
    case u'-':
        return formatNumber(*a - *b);
    case u'*':
        return formatNumber(*a * *b);
    case u'/':
        return formatNumber(*a / *b);
    default:
        break;
    }
    return formatNumber(std::fmod(*a, *b));
}

QString callAdd(const std::vector<Argument> &arguments, const EvalContext &context)
{
    return arithmetic(arguments, context, u'+');
}

QString callSub(const std::vector<Argument> &arguments, const EvalContext &context)
{
    return arithmetic(arguments, context, u'-');
}

QString callMul(const std::vector<Argument> &arguments, const EvalContext &context)
{
    return arithmetic(arguments, context, u'*');
}

QString callDiv(const std::vector<Argument> &arguments, const EvalContext &context)
{
    return arithmetic(arguments, context, u'/');
}

QString callMod(const std::vector<Argument> &arguments, const EvalContext &context)
{
    return arithmetic(arguments, context, u'%');
}

QString callAbs(const std::vector<Argument> &arguments, const EvalContext &context)
{
    const std::optional<double> a = toNumber(argText(arguments, 0, context));
    if (!a.has_value())
        return { }; // T-MATH
    return formatNumber(std::fabs(*a));
}

// "Ignore words" (spec section 4): a word applies when x starts with it
// ignoring case, the next character is a space, and something follows the
// space. The first word in the list that applies is used. Returns the length
// of that word as written in x, or -1 when no word applies.
qsizetype leadingIgnoreWord(const QString &x, const QStringList &words)
{
    for (const QString &word : words) {
        if (word.isEmpty())
            continue;
        if (x.size() <= word.size() + 1)
            continue;
        if (!x.startsWith(word, Qt::CaseInsensitive))
            continue;
        if (x.at(word.size()) != u' ')
            continue;
        return word.size();
    }
    return -1;
}

QString callSort(const std::vector<Argument> &arguments, const EvalContext &context)
{
    const QString x = argText(arguments, 0, context);
    const qsizetype length = leadingIgnoreWord(x, context.options.ignoreWords);
    if (length < 0)
        return x;
    // F-C: the word moves to the end after ", ", as it was written in x.
    return x.mid(length + 1) + QStringLiteral(", ") + x.left(length);
}

QString callRemoveSortWords(const std::vector<Argument> &arguments, const EvalContext &context)
{
    const QString x = argText(arguments, 0, context);
    const qsizetype length = leadingIgnoreWord(x, context.options.ignoreWords);
    if (length < 0)
        return x;
    return x.mid(length + 1);
}

QString callGroup(const std::vector<Argument> &arguments, const EvalContext &context)
{
    const QString x = argText(arguments, 0, context).trimmed();
    const std::optional<int> letters = toCount(argText(arguments, 1, context));
    // T-GROUP: an empty x, an n below 1, or an n that is not a count.
    if (x.isEmpty() || !letters.has_value() || *letters < 1)
        return { };
    const QChar first = x.at(0).toUpper();
    if (first < u'A' || first > u'Z')
        return QStringLiteral("#"); // T-GROUP
    const int position = int(first.unicode()) - int(u'A');
    const int start = (position / *letters) * *letters;
    const int stop = std::min(start + *letters, 26); // T-GROUP: the last bucket stops at Z
    QString out;
    for (int index = start; index < stop; ++index)
        out += QChar(int(u'A') + index);
    return out;
}

// The table, in the order of the spec's section 4 rows. Task 7 adds the last
// row.
constexpr std::array<FunctionSpec, 33> kFunctions { {
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
    { QLatin1StringView("IsNull"), 3, 0, &callIsNull },
    // T-COMPARE: the first argument of $If, and every argument of $And, $Or
    // and $Not, is a condition. -1 means all of them.
    { QLatin1StringView("If"), 3, 1, &callIf },
    { QLatin1StringView("And"), 2, -1, &callAnd },
    { QLatin1StringView("Or"), 2, -1, &callOr },
    { QLatin1StringView("Not"), 1, -1, &callNot },
    { QLatin1StringView("Contains"), 2, 0, &callContains },
    { QLatin1StringView("IsMatch"), 2, 0, &callIsMatch },
    { QLatin1StringView("Replace"), 3, 0, &callReplace },
    { QLatin1StringView("RxReplace"), 3, 0, &callRxReplace },
    { QLatin1StringView("RxMatch"), 2, 0, &callRxMatch },
    { QLatin1StringView("Split"), 3, 0, &callSplit },
    { QLatin1StringView("RSplit"), 3, 0, &callRSplit },
    { QLatin1StringView("RxSplit"), 3, 0, &callRxSplit },
    { QLatin1StringView("Add"), 2, 0, &callAdd },
    { QLatin1StringView("Sub"), 2, 0, &callSub },
    { QLatin1StringView("Mul"), 2, 0, &callMul },
    { QLatin1StringView("Div"), 2, 0, &callDiv },
    { QLatin1StringView("Mod"), 2, 0, &callMod },
    { QLatin1StringView("Abs"), 1, 0, &callAbs },
    { QLatin1StringView("Sort"), 1, 0, &callSort },
    { QLatin1StringView("RemoveSortWords"), 1, 0, &callRemoveSortWords },
    { QLatin1StringView("Group"), 2, 0, &callGroup },
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

// T-MATH: a whole result is written without a decimal point; any other result
// with up to 15 significant digits and no trailing zeros. Never the system
// locale.
QString formatNumber(double value)
{
    if (!std::isfinite(value))
        return { };
    if (value == std::floor(value) && std::fabs(value) < 1e15)
        return QLocale::c().toString(qint64(value));
    return QLocale::c().toString(value, 'g', 15);
}

std::optional<double> toNumber(const QString &text)
{
    // T-MATH: optional white space, an optional '-', digits, and an optional
    // '.' followed by digits, with '.' the decimal point in any locale.
    static const QRegularExpression kPattern(QStringLiteral("^\\s*-?\\d+(\\.\\d+)?\\s*$"));
    if (!kPattern.match(text).hasMatch())
        return std::nullopt;
    bool ok = false;
    const double value = QLocale::c().toDouble(text.trimmed(), &ok);
    if (!ok)
        return std::nullopt;
    return value;
}

} // namespace linuxbee::tmpl
