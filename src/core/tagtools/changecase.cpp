#include "common.h"
#include "tagtools.h"

namespace linuxbee {

namespace {

using tagtools::canonicalField;
using tagtools::setIfChanged;

// A run of characters between white space, or a run of white space.
struct Piece {
    QString text;
    bool word = false;
};

QList<Piece> splitWords(const QString &text)
{
    QList<Piece> pieces;
    qsizetype start = 0;
    while (start < text.size()) {
        const bool space = text.at(start).isSpace();
        qsizetype end = start;
        while (end < text.size() && text.at(end).isSpace() == space)
            ++end;
        pieces.append(Piece { text.mid(start, end - start), !space });
        start = end;
    }
    return pieces;
}

bool isLetterOrDigit(QChar ch)
{
    return ch.isLetter() || ch.isDigit();
}

// G-CASE: the first letter becomes a capital, unless a digit comes before any letter.
QString capitaliseFirstLetter(const QString &word)
{
    for (qsizetype i = 0; i < word.size(); ++i) {
        const QChar ch = word.at(i);
        if (ch.isDigit())
            return word;
        if (ch.isLetter()) {
            QString out = word;
            out.replace(i, 1, QString(ch).toUpper());
            return out;
        }
    }
    return word;
}

QString eachWord(const QString &text)
{
    QString out;
    for (const Piece &piece : splitWords(text))
        out += piece.word ? capitaliseFirstLetter(piece.text) : piece.text;
    return out;
}

// G-CASE: a word with a capital after its first letter is left alone.
bool hasCapitalAfterFirstLetter(const QString &word)
{
    bool seenLetter = false;
    for (const QChar ch : word) {
        if (!ch.isLetter())
            continue;
        if (seenLetter && ch.isUpper())
            return true;
        seenLetter = true;
    }
    return false;
}

// G-CASE: a small word, with characters that are not letters or digits taken
// off both ends, case ignored.
bool isSmallWord(const QString &word, const QStringList &smallWords)
{
    qsizetype begin = 0;
    qsizetype end = word.size();
    while (begin < end && !isLetterOrDigit(word.at(begin)))
        ++begin;
    while (end > begin && !isLetterOrDigit(word.at(end - 1)))
        --end;
    return smallWords.contains(word.mid(begin, end - begin), Qt::CaseInsensitive);
}

QString titleCase(const QString &text, const QStringList &smallWords)
{
    const QList<Piece> pieces = splitWords(text);
    qsizetype words = 0;
    for (const Piece &piece : pieces) {
        if (piece.word)
            ++words;
    }

    QString out;
    qsizetype index = 0;
    for (const Piece &piece : pieces) {
        if (!piece.word) {
            out += piece.text;
            continue;
        }
        const bool first = index == 0;
        const bool last = index == words - 1;
        ++index;
        if (hasCapitalAfterFirstLetter(piece.text))
            out += piece.text;
        else if (!first && !last && isSmallWord(piece.text, smallWords))
            out += piece.text.toLower();
        else
            out += capitaliseFirstLetter(piece.text.toLower());
    }
    return out;
}

}

QStringList defaultSmallWords()
{
    return QStringLiteral(
        "a an and as at but by for from in into nor of off on onto or over per the to via with")
        .split(QLatin1Char(' '));
}

QString changeCase(const QString &text, const CaseOptions &options)
{
    switch (options.style) {
    case CaseStyle::EachWord:
        return eachWord(text);
    case CaseStyle::TitleCase:
        return titleCase(
            text, options.smallWords.isEmpty() ? defaultSmallWords() : options.smallWords);
    case CaseStyle::Sentence:
        return capitaliseFirstLetter(text.toLower());
    case CaseStyle::Upper:
        return text.toUpper();
    case CaseStyle::Lower:
        return text.toLower();
    }
    return text;
}

// G-CASE: an unknown field name gives no changes at all.
TagChanges capitalise(const TrackTags &tags, const CaseOptions &options)
{
    TagChanges changes;
    QStringList fields = options.fields;
    if (fields.isEmpty())
        fields = { QStringLiteral("Title"), QStringLiteral("Artist"), QStringLiteral("Album"),
            QStringLiteral("Album Artist") };

    QStringList canonical;
    for (const QString &name : fields) {
        const QString field = canonicalField(name);
        if (field.isEmpty())
            return changes;
        canonical.append(field);
    }

    for (const QString &field : canonical) {
        QStringList changed;
        for (const QString &value : tags.fields.value(field))
            changed.append(changeCase(value, options));
        setIfChanged(&changes, tags, field, changed);
    }
    return changes;
}

}
