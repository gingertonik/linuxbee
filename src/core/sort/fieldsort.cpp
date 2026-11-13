#include "sort/fieldsort.h"

#include "search/search.h"

#include <QRegularExpression>

// Contract: docs/spec/track-list.md (T-KIND, T-COMPARE). No locale is consulted.
namespace linuxbee {

namespace {

// A value after the rules for its kind: has is false when the value is not a
// number (or a duration, or a size) at all, and such values come last.
struct Parsed {
    bool has = false;
    double value = 0;
};

bool isDigit(QChar c)
{
    return c >= QLatin1Char('0') && c <= QLatin1Char('9');
}

// T-COMPARE, Number: the number the text starts with.
Parsed parseNumberPrefix(const QString &text)
{
    static const QRegularExpression pattern(QStringLiteral("^(-?[0-9]+(?:\\.[0-9]+)?)"));
    const QRegularExpressionMatch match = pattern.match(text.trimmed());
    Parsed result;
    if (match.hasMatch()) {
        result.has = true;
        result.value = match.captured(1).toDouble();
    }
    return result;
}

// T-COMPARE, Duration: m:ss or h:mm:ss, as seconds. Anything else falls back
// to the number rule.
Parsed parseDuration(const QString &text)
{
    static const QRegularExpression twoPart(QStringLiteral("^([0-9]+):([0-9]{2})$"));
    static const QRegularExpression threePart(QStringLiteral("^([0-9]+):([0-9]{2}):([0-9]{2})$"));
    const QString trimmed = text.trimmed();
    Parsed result;
    QRegularExpressionMatch match = threePart.match(trimmed);
    if (match.hasMatch()) {
        const int minutes = match.captured(2).toInt();
        const int seconds = match.captured(3).toInt();
        if (minutes < 60 && seconds < 60) {
            result.has = true;
            result.value = match.captured(1).toDouble() * 3600.0 + minutes * 60.0 + seconds;
        }
        return result;
    }
    match = twoPart.match(trimmed);
    if (match.hasMatch()) {
        const int seconds = match.captured(2).toInt();
        if (seconds < 60) {
            result.has = true;
            result.value = match.captured(1).toDouble() * 60.0 + seconds;
            return result;
        }
    }
    return parseNumberPrefix(trimmed);
}

// T-COMPARE, Size: a number and a unit in steps of 1024, or bytes with no unit.
Parsed parseSize(const QString &text)
{
    static const QRegularExpression pattern(
        QStringLiteral("^(-?[0-9]+(?:\\.[0-9]+)?)\\s*(KB|MB|GB|TB)?$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = pattern.match(text.trimmed());
    Parsed result;
    if (!match.hasMatch())
        return result;
    double factor = 1.0;
    const QString unit = match.captured(2).toUpper();
    if (unit == QStringLiteral("KB"))
        factor = 1024.0;
    else if (unit == QStringLiteral("MB"))
        factor = 1024.0 * 1024.0;
    else if (unit == QStringLiteral("GB"))
        factor = 1024.0 * 1024.0 * 1024.0;
    else if (unit == QStringLiteral("TB"))
        factor = 1024.0 * 1024.0 * 1024.0 * 1024.0;
    result.has = true;
    result.value = match.captured(1).toDouble() * factor;
    return result;
}

int compareParsed(Parsed a, Parsed b)
{
    if (a.has && b.has)
        return a.value < b.value ? -1 : (a.value > b.value ? 1 : 0);
    if (a.has)
        return -1;
    if (b.has)
        return 1;
    return 0;
}

// T-COMPARE, Text, step 2: one ignore word, dropped when the value starts with
// it followed by a space and something more. When more than one matches, the
// longest wins.
QString dropIgnoreWord(const QString &folded, const QStringList &foldedWords)
{
    QString best;
    for (const QString &word : foldedWords) {
        if (word.isEmpty() || word.size() <= best.size())
            continue;
        if (folded.startsWith(word + QLatin1Char(' '))) {
            const QString rest = folded.mid(word.size() + 1).trimmed();
            if (!rest.isEmpty())
                best = word;
        }
    }
    if (best.isEmpty())
        return folded;
    return folded.mid(best.size() + 1).trimmed();
}

// T-COMPARE, Text, step 4: left to right, digit runs by value, other characters
// by code, a value that is the start of the other comes first.
int compareText(const QString &a, const QString &b)
{
    int i = 0;
    int j = 0;
    while (i < a.size() && j < b.size()) {
        const QChar ca = a.at(i);
        const QChar cb = b.at(j);
        if (isDigit(ca) && isDigit(cb)) {
            int ie = i;
            while (ie < a.size() && isDigit(a.at(ie)))
                ++ie;
            int je = j;
            while (je < b.size() && isDigit(b.at(je)))
                ++je;
            // Leading zeros do not count: compare the digits without them.
            int is = i;
            while (is < ie - 1 && a.at(is) == QLatin1Char('0'))
                ++is;
            int js = j;
            while (js < je - 1 && b.at(js) == QLatin1Char('0'))
                ++js;
            const int lenA = ie - is;
            const int lenB = je - js;
            if (lenA != lenB)
                return lenA < lenB ? -1 : 1;
            const int cmp = QStringView(a).mid(is, lenA).compare(QStringView(b).mid(js, lenB));
            if (cmp != 0)
                return cmp < 0 ? -1 : 1;
            i = ie;
            j = je;
            continue;
        }
        if (ca != cb)
            return ca.unicode() < cb.unicode() ? -1 : 1;
        ++i;
        ++j;
    }
    const bool endA = i >= a.size();
    const bool endB = j >= b.size();
    if (endA && endB)
        return 0;
    return endA ? -1 : 1;
}

int compareTextValues(const QString &a, const QString &b, const SortOptions &options)
{
    QStringList foldedWords;
    for (const QString &word : options.ignoreWords)
        foldedWords << foldForSearch(word).trimmed();
    const QString textA = dropIgnoreWord(foldForSearch(a).trimmed(), foldedWords);
    const QString textB = dropIgnoreWord(foldForSearch(b).trimmed(), foldedWords);
    const bool blankA = textA.isEmpty();
    const bool blankB = textB.isEmpty();
    if (blankA || blankB) {
        if (blankA && blankB)
            return 0;
        return blankA ? 1 : -1;
    }
    return compareText(textA, textB);
}

}

FieldKind fieldKind(const QString &fieldName)
{
    const QString name = fieldName.toLower();
    static const QStringList numbers = { QStringLiteral("track#"), QStringLiteral("disc#"),
        QStringLiteral("track count"), QStringLiteral("disc count"), QStringLiteral("bpm"),
        QStringLiteral("play count"), QStringLiteral("skip count"), QStringLiteral("bitrate"),
        QStringLiteral("sample rate"), QStringLiteral("channels"), QStringLiteral("rating") };
    if (numbers.contains(name))
        return FieldKind::Number;
    if (name == QStringLiteral("time"))
        return FieldKind::Duration;
    if (name == QStringLiteral("size"))
        return FieldKind::Size;
    return FieldKind::Text;
}

int compareFieldValues(
    const QString &fieldName, const QString &a, const QString &b, const SortOptions &options)
{
    switch (fieldKind(fieldName)) {
    case FieldKind::Number:
        return compareParsed(parseNumberPrefix(a), parseNumberPrefix(b));
    case FieldKind::Duration:
        return compareParsed(parseDuration(a), parseDuration(b));
    case FieldKind::Size:
        return compareParsed(parseSize(a), parseSize(b));
    case FieldKind::Text:
        break;
    }
    return compareTextValues(a, b, options);
}

// Plan 11 task 3 replaces this body.
QString formatTrackSummary(int count, qint64 totalBytes, qint64 totalMs)
{
    Q_UNUSED(count);
    Q_UNUSED(totalBytes);
    Q_UNUSED(totalMs);
    return { };
}

}
