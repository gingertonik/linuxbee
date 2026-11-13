#include "tagwriter.h"

#include <QFile>
#include <QFileInfo>

#include <fileref.h>
#include <tpropertymap.h>
#include <tstringlist.h>

#include "catalogue.h"
#include "changes.h"
#include "tagreader.h"

namespace linuxbee {

namespace {

using tags::KeyFamily;

// W-TABLE: the family decides the keys. MP3 and WAV carry ID3v2; FLAC, Ogg and
// Opus carry Vorbis comments; MPEG-4 and WavPack carry atoms and APEv2 items.
KeyFamily familyOf(const QString &format)
{
    if (format == QLatin1String("mp3") || format == QLatin1String("wav"))
        return KeyFamily::Id3v2;
    if (format == QLatin1String("flac") || format == QLatin1String("ogg")
        || format == QLatin1String("opus"))
        return KeyFamily::Vorbis;
    return KeyFamily::Mp4Ape;
}

TagLib::String tagString(const QString &text)
{
    const QByteArray utf8 = text.toUtf8();
    return TagLib::String(utf8.constData(), TagLib::String::UTF8);
}

TagLib::StringList tagList(const QStringList &values)
{
    TagLib::StringList list;
    for (const QString &value : values)
        list.append(tagString(value));
    return list;
}

bool isNumberField(const QString &field)
{
    return field == QLatin1String("Track#") || field == QLatin1String("Track Count")
        || field == QLatin1String("Disc#") || field == QLatin1String("Disc Count");
}

// W-NUMBERS: "n/total"; a count with no number is "/total", and a bare n has no count.
QString composeNumber(const QString &number, const QString &total)
{
    if (total.isEmpty())
        return number;
    return number + QLatin1Char('/') + total;
}

// W-NUMBERS: a number and its count are rewritten together, from the values the
// file will have afterwards. Vorbis comments keep them apart; the other families
// hold one "n/total" text under the number's key.
void rewriteNumbers(TagLib::PropertyMap &map, const QString &numberField, const QString &totalField,
    const TrackTags &after, KeyFamily family)
{
    for (const QString &key : tags::readKeys(numberField, family))
        map.erase(tagString(key));
    for (const QString &key : tags::readKeys(totalField, family))
        map.erase(tagString(key));

    const QString number = after.first(numberField);
    const QString total = after.first(totalField);
    if (family == KeyFamily::Vorbis) {
        if (!number.isEmpty())
            map.insert(tagString(tags::writeKey(numberField, family)), tagList({ number }));
        if (!total.isEmpty())
            map.insert(tagString(tags::writeKey(totalField, family)), tagList({ total }));
        return;
    }
    const QString text = composeNumber(number, total);
    if (!text.isEmpty())
        map.insert(tagString(tags::writeKey(numberField, family)), tagList({ text }));
}

// W-RATING, every format but ID3v2: the RATING key holds stars times 20.
void rewriteRating(TagLib::PropertyMap &map, const TrackTags &after)
{
    const TagLib::String key = tagString(QStringLiteral("RATING"));
    map.erase(key);
    bool parsed = false;
    const double stars = after.first(QStringLiteral("Rating")).toDouble(&parsed);
    if (!parsed)
        return;
    map.insert(key, tagList({ QString::number(qRound(stars * 20.0)) }));
}

// Step 5 of spec section 1, for the fields and other tags only. Pictures and the
// ID3 options come with their own tasks.
bool saveChanges(const QString &filePath, const TrackTags &now, const tags::CheckedChanges &checked,
    const TrackTags &after)
{
    const KeyFamily family = familyOf(now.format);
    const QByteArray name = QFile::encodeName(filePath);
    TagLib::FileRef ref(name.constData(), false);
    if (ref.isNull() || !ref.file() || !ref.file()->isValid())
        return false;
    TagLib::File *file = ref.file();
    TagLib::PropertyMap map = file->properties();

    if (checked.fields.contains(QStringLiteral("Track#"))
        || checked.fields.contains(QStringLiteral("Track Count"))) {
        rewriteNumbers(map, QStringLiteral("Track#"), QStringLiteral("Track Count"), after, family);
    }
    if (checked.fields.contains(QStringLiteral("Disc#"))
        || checked.fields.contains(QStringLiteral("Disc Count"))) {
        rewriteNumbers(map, QStringLiteral("Disc#"), QStringLiteral("Disc Count"), after, family);
    }

    for (auto it = checked.fields.cbegin(); it != checked.fields.cend(); ++it) {
        const QString &field = it.key();
        if (isNumberField(field))
            continue;
        if (field == QLatin1String("Rating")) {
            // POPM (ID3v2) is written by its own rule, in a later task.
            if (family != KeyFamily::Id3v2)
                rewriteRating(map, after);
            continue;
        }
        // W-ONEHOME: every key the field is read from goes before the new one.
        for (const QString &key : tags::readKeys(it.key(), family))
            map.erase(tagString(key));
        const QStringList values = after.fields.value(field);
        if (!values.isEmpty())
            map.insert(tagString(tags::writeKey(field, family)), tagList(values));
    }

    for (auto it = checked.other.cbegin(); it != checked.other.cend(); ++it) {
        const TagLib::String key = tagString(it.key());
        map.erase(key);
        if (!it.value().isEmpty())
            map.insert(key, tagList(it.value()));
    }

    file->setProperties(map);
    return file->save();
}

}

WriteResult writeTags(
    const QString &filePath, const TagChanges &changes, const WriteOptions &options)
{
    Q_UNUSED(options);

    // W-STATUS: the read readTags does.
    const ReadResult read = readTags(filePath);
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

    // W-UNCHANGED: when the file already says this, it is not opened for writing.
    const TrackTags after = tags::applyTo(read.tags, checked, changes.pictures);
    if (tags::sameTags(read.tags, after)) {
        result.status = WriteResult::Status::Unchanged;
        return result;
    }

    if (!QFileInfo(filePath).isWritable()) {
        result.status = WriteResult::Status::NotWritable;
        return result;
    }

    bool saved = false;
    try {
        saved = saveChanges(filePath, read.tags, checked, after);
    } catch (...) {
        // writeTags never throws; a tag library failure is Failed.
        saved = false;
    }
    result.status = saved ? WriteResult::Status::Ok : WriteResult::Status::Failed;
    return result;
}

}
