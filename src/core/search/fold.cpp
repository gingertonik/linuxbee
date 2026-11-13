#include "search.h"

namespace linuxbee {

// S-FOLD: compatibility decomposition (NFKD), combining marks removed, then
// lower case. Spaces and punctuation are kept.
QString foldForSearch(const QString &text)
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

// S-FIELDS: Title, Artist, Album, Album Artist, Genre, Composer, Year,
// Comment, Filename.
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
    };
}

}
