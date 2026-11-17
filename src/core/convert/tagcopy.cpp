#include "tagcopy.h"

namespace linuxbee::convert {

namespace {

// C-TAGS: the other tags that describe the source's encoding, not its music.
bool describesEncoding(const QString &key)
{
    return key == QLatin1String("ENCODER") || key == QLatin1String("ENCODING")
        || key == QLatin1String("ENCODERSETTINGS") || key == QLatin1String("LENGTH");
}

// Drops the entry that a problem line from writeTags is about: a picture, named
// "picture N:" counting from 1, or the field or key the line starts with.
// Returns false when the line matches no entry.
bool dropRefused(TagChanges &changes, const QString &problem)
{
    const QString picturePrefix = QStringLiteral("picture ");
    if (problem.startsWith(picturePrefix)) {
        const int colon = problem.indexOf(QLatin1Char(':'));
        bool ok = false;
        const int number = colon < 0
            ? 0
            : problem.mid(picturePrefix.size(), colon - picturePrefix.size()).toInt(&ok);
        if (!changes.pictures || !ok || number < 1 || number > changes.pictures->size())
            return false;
        changes.pictures->removeAt(number - 1);
        return true;
    }

    // The longest field or other key that the line starts with, then a colon.
    QString best;
    bool isField = false;
    for (auto it = changes.fields.cbegin(); it != changes.fields.cend(); ++it) {
        if (problem.startsWith(it.key() + QLatin1Char(':')) && it.key().size() > best.size()) {
            best = it.key();
            isField = true;
        }
    }
    for (auto it = changes.other.cbegin(); it != changes.other.cend(); ++it) {
        if (problem.startsWith(it.key() + QLatin1Char(':')) && it.key().size() > best.size()) {
            best = it.key();
            isField = false;
        }
    }
    if (best.isEmpty())
        return false;
    if (isField)
        changes.fields.remove(best);
    else
        changes.other.remove(best);
    return true;
}

}

QString copyTags(const TrackTags &source, const QString &target, bool pictures,
    const WriteOptions &options, QStringList *warnings)
{
    // Only the target's other keys matter here, so its pictures are not read.
    ReadOptions readOptions;
    readOptions.pictures = false;
    const ReadResult current = readTags(target, readOptions);
    if (current.status != ReadResult::Status::Ok)
        return QStringLiteral("the converted file could not be read");

    TagChanges changes;
    // Every catalogue field takes the source's values; a field the source lacks
    // is removed, because an empty list removes it.
    for (const QString &name : tagFieldNames())
        changes.fields.insert(name, source.fields.value(name));

    for (auto it = source.other.cbegin(); it != source.other.cend(); ++it) {
        if (!describesEncoding(it.key()))
            changes.other.insert(it.key(), it.value());
    }
    // Every other tag the target has that is not copied goes. That clears the
    // ENCODER comment ffmpeg writes for itself.
    for (auto it = current.tags.other.cbegin(); it != current.tags.other.cend(); ++it) {
        if (!changes.other.contains(it.key()))
            changes.other.insert(it.key(), QStringList());
    }

    if (pictures)
        changes.pictures = source.pictures;

    for (;;) {
        const WriteResult result = writeTags(target, changes, options);
        if (result.ok())
            return { };
        // WS-LINK, WS-SAME: a file that is not saved says why.
        if ((result.status == WriteResult::Status::HardLinked
                || result.status == WriteResult::Status::ChangedOnDisk)
            && !result.problems.isEmpty())
            return QStringLiteral("the tags could not be written: %1").arg(result.problems.first());
        if (result.status != WriteResult::Status::InvalidChange || result.problems.isEmpty())
            return QStringLiteral("the tags could not be written");

        // A refused entry is dropped and reported; the rest is written on the next pass.
        for (const QString &problem : result.problems) {
            if (!dropRefused(changes, problem))
                return QStringLiteral("the tags could not be written: %1").arg(problem);
            if (warnings)
                warnings->append(QStringLiteral("not copied: ") + problem);
        }
    }
}

}
