#include "fold_p.h"
#include "search.h"

namespace linuxbee {

namespace {

// The full fold of S-FOLD for any text: compatibility decomposition (NFKD), combining
// marks removed, then lower case.
QString fullFold(const QString &text)
{
    const QString decomposed = text.normalized(QString::NormalizationForm_KD);
    QString kept;
    kept.reserve(decomposed.size());
    for (const QChar c : decomposed) {
        const QChar::Category category = c.category();
        if (category == QChar::Mark_NonSpacing || category == QChar::Mark_SpacingCombining
            || category == QChar::Mark_Enclosing)
            continue;
        kept.append(c);
    }
    return kept.toLower();
}

// A character below U+0080 is unchanged by NFKD and is no mark, so only A to Z change (to
// lower case). Each run of other characters gets the full fold, alone: NFKD reorders only
// marks, and an ASCII character is no mark, so no run reaches into the next.
QString foldRuns(const QString &text)
{
    const qsizetype size = text.size();
    const QChar *data = text.constData();

    qsizetype i = 0;
    while (i < size && data[i].unicode() < 0x80
        && !(data[i].unicode() >= 'A' && data[i].unicode() <= 'Z'))
        ++i;
    if (i == size)
        return text; // nothing to change: the text itself, shared

    QString out;
    out.reserve(size);
    out.append(data, i);
    while (i < size) {
        const char16_t u = data[i].unicode();
        if (u < 0x80) {
            out.append(QChar(static_cast<char16_t>(u >= 'A' && u <= 'Z' ? u + 0x20 : u)));
            ++i;
            continue;
        }
        qsizetype end = i + 1;
        while (end < size && data[end].unicode() >= 0x80)
            ++end;
        out.append(fullFold(text.mid(i, end - i)));
        i = end;
    }
    return out;
}

}

QString foldKeepingUnit(const QString &text)
{
    return foldRuns(text);
}

// S-FOLD: U+001F becomes a space; then compatibility decomposition (NFKD), combining
// marks removed, then lower case. Spaces and punctuation are kept.
QString foldForSearch(const QString &text)
{
    if (!text.contains(QChar(0x1F)))
        return foldRuns(text);
    QString plain = text;
    plain.replace(QChar(0x1F), QLatin1Char(' '));
    return foldRuns(plain);
}

// S-FIELDS: every text field of the catalogue but Lyrics, and Filename, 29 in all: the
// nine of before, then the rest.
QStringList defaultSearchFields()
{
    return {
        QStringLiteral("Title"),
        QStringLiteral("Artist"),
        QStringLiteral("Album"),
        QStringLiteral("Album Artist"),
        QStringLiteral("Genre"),
        QStringLiteral("Composer"),
        QStringLiteral("Year"),
        QStringLiteral("Comment"),
        QStringLiteral("Filename"),
        QStringLiteral("Conductor"),
        QStringLiteral("Publisher"),
        QStringLiteral("Grouping"),
        QStringLiteral("Mood"),
        QStringLiteral("Lyricist"),
        QStringLiteral("Copyright"),
        QStringLiteral("ISRC"),
        QStringLiteral("Sort Title"),
        QStringLiteral("Sort Artist"),
        QStringLiteral("Sort Album"),
        QStringLiteral("Sort Album Artist"),
        QStringLiteral("Sort Composer"),
        QStringLiteral("Original Year"),
        QStringLiteral("Original Artist"),
        QStringLiteral("Original Album"),
        QStringLiteral("Subtitle"),
        QStringLiteral("Language"),
        QStringLiteral("Initial Key"),
        QStringLiteral("Encoded By"),
        QStringLiteral("Mix Artist"),
    };
}

}
