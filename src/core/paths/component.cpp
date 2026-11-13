#include "component.h"

#include "pathbuilder.h"

#include <QHash>

namespace linuxbee::paths {

QString removeControlCharacters(const QString &text)
{
    QString out;
    out.reserve(text.size());
    const int n = text.size();
    for (int i = 0; i < n; ++i) {
        const QChar unit = text.at(i);
        if (unit.isHighSurrogate()) {
            if (i + 1 < n && text.at(i + 1).isLowSurrogate()) {
                out.append(unit);
                out.append(text.at(i + 1));
                ++i;
            }
            // A lone high surrogate is removed.
            continue;
        }
        if (unit.isLowSurrogate()) {
            continue; // unpaired: its high half was consumed above, if any
        }
        if (unit.unicode() < 0x0020 || unit.unicode() == 0x007F) {
            continue; // P-CTRL removes, it does not replace
        }
        out.append(unit);
    }
    return out;
}

QString trimSpacesAndDots(const QString &text)
{
    int start = 0;
    int end = text.size();
    while (
        start < end && (text.at(start) == QLatin1Char(' ') || text.at(start) == QLatin1Char('.'))) {
        ++start;
    }
    while (end > start
        && (text.at(end - 1) == QLatin1Char(' ') || text.at(end - 1) == QLatin1Char('.'))) {
        --end;
    }
    return text.mid(start, end - start);
}

QString fitToBytes(const QString &text, int maxBytes)
{
    if (text.toUtf8().size() <= maxBytes)
        return text;
    int bytes = 0;
    int units = 0;
    const int n = text.size();
    int i = 0;
    while (i < n) {
        const ushort code = text.at(i).unicode();
        int charBytes = 1;
        int charUnits = 1;
        if (QChar::isHighSurrogate(code) && i + 1 < n
            && QChar::isLowSurrogate(text.at(i + 1).unicode())) {
            charBytes = 4; // one code point above U+FFFF: two units, 4 bytes
            charUnits = 2;
        } else if (code >= 0x0800) {
            charBytes = 3;
        } else if (code >= 0x0080) {
            charBytes = 2;
        }
        if (bytes + charBytes > maxBytes)
            break;
        bytes += charBytes;
        units += charUnits;
        i += charUnits;
    }
    return trimSpacesAndDots(text.left(units));
}

bool isReservedName(const QString &base)
{
    static const QStringList names = [] {
        QStringList list = { QStringLiteral("CON"), QStringLiteral("PRN"), QStringLiteral("AUX"),
            QStringLiteral("NUL") };
        for (int i = 1; i <= 9; ++i) {
            list << QStringLiteral("COM") + QString::number(i);
            list << QStringLiteral("LPT") + QString::number(i);
        }
        return list;
    }();
    return names.contains(base.toUpper());
}

} // namespace linuxbee::paths

namespace linuxbee {

namespace {

// P-MAP: one pass over the text. The space waits for P-SPACES. Every
// separator is removed, from the text and from each replacement. A
// replacement is never mapped again.
QString mapCharacters(const QString &text, const QHash<QChar, QString> &map)
{
    QString out;
    out.reserve(text.size());
    const int n = text.size();
    for (int i = 0; i < n; ++i) {
        const QChar unit = text.at(i);
        if (unit.isHighSurrogate() && i + 1 < n && text.at(i + 1).isLowSurrogate()) {
            out.append(unit);
            out.append(text.at(i + 1));
            ++i;
            continue;
        }
        if (unit == QLatin1Char(' ')) {
            out.append(unit); // P-MAP skips the space; P-SPACES handles it
            continue;
        }
        const auto entry = map.constFind(unit);
        if (entry == map.constEnd()) {
            if (unit != QLatin1Char('\\') && unit != QLatin1Char('/'))
                out.append(unit);
            continue;
        }
        for (const QChar &replacement : *entry) {
            if (replacement != QLatin1Char('\\') && replacement != QLatin1Char('/'))
                out.append(replacement);
        }
    }
    return out;
}

// P-SPACES step 1: every run of white space becomes one space.
QString collapseSpaceRuns(const QString &text)
{
    QString out;
    out.reserve(text.size());
    bool inRun = false;
    for (const QChar &unit : text) {
        if (unit.isSpace()) {
            if (!inRun)
                out.append(QLatin1Char(' '));
            inRun = true;
            continue;
        }
        inRun = false;
        out.append(unit);
    }
    return out;
}

// P-RESERVED: insert an underscore right after a reserved base.
QString unreserveName(const QString &text)
{
    const int dot = text.indexOf(QLatin1Char('.'));
    const QString base = dot < 0 ? text : text.left(dot);
    if (!paths::isReservedName(base))
        return text;
    QString out = text;
    out.insert(dot < 0 ? out.size() : dot, QLatin1Char('_'));
    return out;
}

} // namespace

QString cleanComponent(const QString &text, const PathRules &rules)
{
    QString cleaned = paths::removeControlCharacters(text); // P-CTRL
    cleaned = mapCharacters(cleaned, rules.characterMap); // P-MAP
    cleaned = collapseSpaceRuns(cleaned); // P-SPACES 1
    cleaned = paths::trimSpacesAndDots(cleaned); // P-SPACES 2: P-TRIM
    const auto space = rules.characterMap.constFind(QLatin1Char(' '));
    if (space != rules.characterMap.constEnd()) {
        // P-MAP rule: no replacement may smuggle in a separator, the
        // space's own included. Clean a copy, then apply it.
        QString replacement = *space;
        replacement.remove(QLatin1Char('\\'));
        replacement.remove(QLatin1Char('/'));
        cleaned.replace(QLatin1Char(' '), replacement); // P-SPACES 3, after P-TRIM
    }
    if (rules.avoidReservedNames)
        cleaned = unreserveName(cleaned); // P-RESERVED
    return paths::fitToBytes(cleaned, rules.maxComponentBytes); // P-LENGTH
}

} // namespace linuxbee
