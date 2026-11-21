#pragma once
#include <QHash>
#include <QString>
#include <QStringList>

#include <functional>

#include "template/template.h"

// Turns a naming template and one track's tags into a relative file path.
// Contract: docs/spec/paths.md. Nothing here touches the disk.
namespace linuxbee {

struct PathRules {
    // P-MAP: the characters that are replaced, and what replaces each. An
    // empty replacement removes the character. See defaultCharacterMap().
    static QHash<QChar, QString> defaultCharacterMap();
    QHash<QChar, QString> characterMap = defaultCharacterMap();

    // P-RESERVED: rename components that are device names on Windows.
    bool avoidReservedNames = true;

    // P-LENGTH: the longest a single folder or file name may be, in UTF-8 bytes.
    int maxComponentBytes = 255;

    // P-MISSING: when true, a track with a blank top-level field is refused
    // (Status::MissingTags). When false, the field renders as "Unknown <name>".
    bool skipWhenTagsMissing = true;

    // P-PLACEHOLDER: a blank top-level field named here is written as its text, in either
    // way of skipWhenTagsMissing, and never counts as missing. By default Disc-Track# is `#`.
    QHash<QString, QString> blankAs = { { QStringLiteral("Disc-Track#"), QStringLiteral("#") } };

    // Ignore words and the multi-value separator for the template.
    // unknownForBlank is set from skipWhenTagsMissing and is ignored here.
    TemplateOptions templateOptions;
};

struct BuiltPath {
    enum class Status { Ok, InvalidTemplate, MissingTags, EmptyName };
    Status status = Status::EmptyName;

    // Status::Ok only. Components are joined with '/'. Never starts with '/',
    // never has an empty, "." or ".." component, and includes the extension.
    QString relativePath;

    // Status::MissingTags only: the blank top-level fields, in template order.
    QStringList missingFields;

    // Status::InvalidTemplate only.
    TemplateError templateError;
};

// `extension` is given without regard to a leading dot or to case ("mp3",
// ".MP3"); an empty extension gives a name with no extension.
BuiltPath buildRelativePath(const Template &pathTemplate, const TagSource &tags,
    const QString &extension, const PathRules &rules = { });

// Cleans one folder or file name (no separators, no extension handling).
// May return an empty string.
QString cleanComponent(const QString &text, const PathRules &rules = { });

// P-DUP: returns `relativePath` when `exists(relativePath)` is false, otherwise
// the first of name_1, name_2, ... that does not exist. Returns an empty
// string when 9999 candidates all exist.
QString uniqueRelativePath(const QString &relativePath,
    const std::function<bool(const QString &)> &exists, const PathRules &rules = { });

}
