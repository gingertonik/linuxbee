#pragma once
#include <QString>
#include <QStringList>

#include <optional>

#include "tags/tagreader.h"
#include "template/template.h"

// The fields a template can use for one file on disk: its tags, plus the
// fields worked out from them and from the file itself (Disc-Track#,
// Filename, Year (yyyy), ...). Contract: docs/spec/track-fields.md.
// Nothing here reads the disk.
namespace linuxbee {

struct TrackFieldOptions {
    // F-DISCTRACK: digits the disc and track numbers are padded to.
    int discDigits = 2;
    int trackDigits = 2;
};

class TrackFieldSource : public TagSource {
public:
    // `filePath` is the file's absolute path with '/' separators. It may be
    // empty, and then the file fields are blank. Both references must outlive
    // this object.
    TrackFieldSource(
        const TrackTags &tags, const QString &filePath, const TrackFieldOptions &options = { });

    std::optional<QString> value(const QString &fieldName) const override;

private:
    const TrackTags &m_tags;
    QString m_filePath;
    TrackFieldOptions m_options;
};

// F-NAMES: every name TrackFieldSource knows: the tag catalogue, then the
// computed fields, in the order of the spec's table.
QStringList trackFieldNames();

// F-TIME: 0:07, 3:05, 1:02:03.
QString formatDuration(int milliseconds);

}
