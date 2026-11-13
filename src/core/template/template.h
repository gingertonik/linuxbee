#pragma once
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

namespace linuxbee {

// Supplies field values to a template. One implementation per data source
// (a library track, a file on disk, a test map).
class TagSource {
public:
    virtual ~TagSource() = default;
    // nullopt: fieldName is not a field this source knows about.
    // Empty string: the field is known but blank for this track.
    // Multi-value fields are returned joined with TemplateOptions::multiValueSeparator.
    virtual std::optional<QString> value(const QString &fieldName) const = 0;
};

struct TemplateOptions {
    // When true, a blank field written at the top level of the template
    // renders as "Unknown <field name>". Fields inside function arguments
    // are never replaced. See docs/spec/templates.md, T-BLANK.
    bool unknownForBlank = false;
    QStringList ignoreWords = { QStringLiteral("The") };
    QString multiValueSeparator = QStringLiteral("; ");
};

struct TemplateError {
    int position = -1; // 0-based index into the template text; -1 means no error
    QString message;
};

class Template {
public:
    // Never throws. On a syntax error the result has isValid() == false.
    static Template parse(const QString &text);

    bool isValid() const;
    TemplateError error() const;

    // Returns an empty string when the template is not valid.
    QString evaluate(const TagSource &tags, const TemplateOptions &options = { }) const;

    // Field names in order of first appearance, without the angle brackets,
    // each listed once.
    QStringList referencedFields() const;

    // The fields written outside every function call, in order of first
    // appearance, each listed once. Empty for an invalid template.
    // See docs/spec/paths.md, P-MISSING.
    QStringList topLevelFields() const;

    struct Node; // defined in template.cpp

private:
    std::shared_ptr<const Node> m_root;
    TemplateError m_error;
};

}
