#include "tagwriter.h"

#include <QFileInfo>

#include "changes.h"
#include "tagreader.h"

namespace linuxbee {

// Steps 1, 2 and 4 of spec section 1. Saving (step 5) comes with the field and ID3 tasks.
WriteResult writeTags(
    const QString &filePath, const TagChanges &changes, const WriteOptions &options)
{
    Q_UNUSED(options);

    // W-STATUS: the same read readTags does; pictures are not needed here.
    ReadOptions readOptions;
    readOptions.pictures = false;
    const ReadResult read = readTags(filePath, readOptions);
    WriteResult result;
    if (read.status == ReadResult::Status::NotFound) {
        result.status = WriteResult::Status::NotFound;
        return result;
    }
    if (read.status == ReadResult::Status::NotAudio) {
        result.status = WriteResult::Status::NotAudio;
        return result;
    }

    // W-INVALID: one refused entry refuses the whole change; nothing is written.
    const tags::CheckedChanges checked = tags::checkChanges(changes);
    if (!checked.problems.isEmpty()) {
        result.status = WriteResult::Status::InvalidChange;
        result.problems = checked.problems;
        return result;
    }

    if (!QFileInfo(filePath).isWritable()) {
        result.status = WriteResult::Status::NotWritable;
        return result;
    }

    // The save itself is not written yet.
    result.status = WriteResult::Status::Failed;
    return result;
}

}
