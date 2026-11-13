#include "pathbuilder.h"

#include "component.h"

#include <QHash>

#include <optional>

namespace linuxbee {

QHash<QChar, QString> PathRules::defaultCharacterMap()
{
    // W-O: the characters illegal in Windows file names, each replaced by a
    // single space by default.
    QHash<QChar, QString> map;
    const QString characters = QStringLiteral("\"|?<:\\>*/");
    for (const QChar &character : characters)
        map.insert(character, QStringLiteral(" "));
    return map;
}

namespace {

// P-SEP: a wrapper over the caller's source. In each known value every '\'
// and '/' becomes that character's map entry (removed when the map has no
// entry for it), and control characters are removed (P-CTRL). An unknown
// field is returned unchanged as nullopt.
class FilteredTagSource final : public TagSource {
public:
    FilteredTagSource(const TagSource &inner, const QHash<QChar, QString> &map)
        : m_inner(inner)
        , m_map(map)
    {
    }

    std::optional<QString> value(const QString &fieldName) const override
    {
        const std::optional<QString> known = m_inner.value(fieldName);
        if (!known.has_value())
            return std::nullopt;
        QString out;
        out.reserve(known->size());
        const int n = known->size();
        for (int i = 0; i < n; ++i) {
            const QChar unit = known->at(i);
            if (unit.isHighSurrogate() && i + 1 < n && known->at(i + 1).isLowSurrogate()) {
                out.append(unit);
                out.append(known->at(i + 1));
                ++i;
                continue;
            }
            if (unit == QLatin1Char('\\') || unit == QLatin1Char('/')) {
                const auto entry = m_map.constFind(unit);
                if (entry != m_map.constEnd()) {
                    for (const QChar &replacement : *entry) {
                        if (replacement != QLatin1Char('\\') && replacement != QLatin1Char('/'))
                            out.append(replacement);
                    }
                }
                continue;
            }
            out.append(unit);
        }
        return paths::removeControlCharacters(out);
    }

private:
    const TagSource &m_inner;
    const QHash<QChar, QString> &m_map;
};

// P-SEP: split at every '\' and every '/'.
QStringList splitComponents(const QString &text)
{
    QStringList parts;
    QString current;
    for (const QChar &unit : text) {
        if (unit == QLatin1Char('\\') || unit == QLatin1Char('/')) {
            parts.append(current);
            current.clear();
        } else {
            current.append(unit);
        }
    }
    parts.append(current);
    return parts;
}

} // namespace

BuiltPath buildRelativePath(const Template &pathTemplate, const TagSource &tags,
    const QString &extension, const PathRules &rules)
{
    BuiltPath result;
    if (!pathTemplate.isValid()) {
        result.status = BuiltPath::Status::InvalidTemplate;
        result.templateError = pathTemplate.error();
        return result;
    }

    // P-MISSING: blank top-level fields on the caller's source. A field the
    // source does not know is not blank (T-UNKNOWN prints it later).
    if (rules.skipWhenTagsMissing) {
        QStringList missing;
        for (const QString &field : pathTemplate.topLevelFields()) {
            const std::optional<QString> value = tags.value(field);
            if (value.has_value() && value->trimmed().isEmpty())
                missing.append(field);
        }
        if (!missing.isEmpty()) {
            result.status = BuiltPath::Status::MissingTags;
            result.missingFields = missing; // in template order
            return result;
        }
    }

    FilteredTagSource filtered(tags, rules.characterMap);
    TemplateOptions options = rules.templateOptions;
    options.unknownForBlank = !rules.skipWhenTagsMissing;
    const QString text = pathTemplate.evaluate(filtered, options);

    QStringList components;
    for (const QString &piece : splitComponents(text)) {
        const QString cleaned = cleanComponent(piece, rules);
        if (!cleaned.isEmpty())
            components.append(cleaned); // P-EMPTY drops the empty pieces
    }
    if (components.isEmpty())
        return result; // Status::EmptyName, relativePath stays empty

    // P-EXT and the file name length limit are task 5; the extension is
    // appended as given for now.
    QString fileName = components.takeLast();
    if (!extension.isEmpty())
        fileName += QLatin1Char('.') + extension;
    components.append(fileName);

    result.relativePath = components.join(QLatin1Char('/'));
    result.status = BuiltPath::Status::Ok;
    return result;
}

QString uniqueRelativePath(const QString &relativePath,
    const std::function<bool(const QString &)> &exists, const PathRules &rules)
{
    Q_UNUSED(relativePath);
    Q_UNUSED(exists);
    Q_UNUSED(rules);
    return { };
}

} // namespace linuxbee
