#include "tagwriter.h"

#include <memory>

#include <QFile>
#include <QFileInfo>

#include <fileref.h>
#include <id3v2.h>
#include <id3v2tag.h>
#include <mpegfile.h>
#include <popularimeterframe.h>
#include <tpropertymap.h>
#include <tstringlist.h>
#include <wavfile.h>

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

// W-ID3VER: the ID3v2 version a save writes, whatever version the file had.
TagLib::ID3v2::Version id3Version(const WriteOptions &options)
{
    return options.id3Version == WriteOptions::Id3Version::V24 ? TagLib::ID3v2::v4
                                                               : TagLib::ID3v2::v3;
}

// W-RATING, ID3v2: the address and counter of the first POPM frame. They are read
// before setProperties, which may drop the frame.
struct PopmKeep {
    TagLib::String email;
    unsigned int counter = 0;
};

PopmKeep popmKeepOf(TagLib::File *file)
{
    PopmKeep keep;
    const TagLib::ID3v2::Tag *tag = nullptr;
    if (auto *mpeg = dynamic_cast<TagLib::MPEG::File *>(file)) {
        if (mpeg->hasID3v2Tag())
            tag = mpeg->ID3v2Tag();
    } else if (auto *wav = dynamic_cast<TagLib::RIFF::WAV::File *>(file)) {
        if (wav->hasID3v2Tag())
            tag = wav->ID3v2Tag();
    }
    if (!tag)
        return keep;

    const TagLib::ID3v2::FrameList frames = tag->frameList(TagLib::ByteVector("POPM"));
    if (frames.isEmpty())
        return keep;
    if (const auto *popm
        = dynamic_cast<const TagLib::ID3v2::PopularimeterFrame *>(frames.front())) {
        keep.email = popm->email();
        keep.counter = popm->counter();
    }
    return keep;
}

// The ID3v2 tag that holds the rating. A removal does not create a tag.
TagLib::ID3v2::Tag *id3v2TagFor(TagLib::File *file, bool create)
{
    if (auto *mpeg = dynamic_cast<TagLib::MPEG::File *>(file))
        return mpeg->ID3v2Tag(create);
    if (auto *wav = dynamic_cast<TagLib::RIFF::WAV::File *>(file))
        return wav->ID3v2Tag();
    return nullptr;
}

// W-RATING, ID3v2: one POPM frame holds the rating byte. Its address and counter
// are the kept ones, and a new frame has an empty address and a counter of 0.
void rewritePopm(TagLib::ID3v2::Tag *tag, const PopmKeep &keep, const TrackTags &after)
{
    tag->removeFrames(TagLib::ByteVector("POPM"));
    bool parsed = false;
    const double stars = after.first(QStringLiteral("Rating")).toDouble(&parsed);
    const int byte = parsed ? popmFromRating(stars) : 0;
    if (byte == 0)
        return;
    auto frame = std::make_unique<TagLib::ID3v2::PopularimeterFrame>();
    frame->setEmail(keep.email);
    frame->setRating(byte);
    frame->setCounter(keep.counter);
    tag->addFrame(frame.release());
}

// W-ID3V1, W-ID3VER, W-WAV: how an ID3 file is saved. Other families use TagLib's
// default save.
bool saveId3(TagLib::File *file, const WriteOptions &options)
{
    if (auto *mpeg = dynamic_cast<TagLib::MPEG::File *>(file)) {
        int kinds = TagLib::MPEG::File::ID3v2;
        if (options.writeId3v1)
            kinds |= TagLib::MPEG::File::ID3v1;
        // An APEv2 tag is not part of this change: it stays if the file has one.
        if (mpeg->hasAPETag())
            kinds |= TagLib::MPEG::File::APE;
        return mpeg->save(kinds, TagLib::File::StripOthers, id3Version(options),
            options.writeId3v1 ? TagLib::File::Duplicate : TagLib::File::DoNotDuplicate);
    }
    if (auto *wav = dynamic_cast<TagLib::RIFF::WAV::File *>(file)) {
        // W-WAV: the ID3 chunk only. No RIFF Info chunk is created, and none is stripped.
        return wav->save(
            TagLib::RIFF::WAV::File::ID3v2, TagLib::File::StripNone, id3Version(options));
    }
    return file->save();
}

// Step 5 of spec section 1: edits the property map, then saves. Pictures come with
// their own task.
bool saveChanges(const QString &filePath, const TrackTags &now, const tags::CheckedChanges &checked,
    const TrackTags &after, const WriteOptions &options)
{
    const KeyFamily family = familyOf(now.format);
    const QByteArray name = QFile::encodeName(filePath);
    TagLib::FileRef ref(name.constData(), false);
    if (ref.isNull() || !ref.file() || !ref.file()->isValid())
        return false;
    TagLib::File *file = ref.file();

    const bool id3 = family == KeyFamily::Id3v2;
    const bool ratingTouched = checked.fields.contains(QStringLiteral("Rating"));
    const PopmKeep keep = id3 && ratingTouched ? popmKeepOf(file) : PopmKeep { };
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
            // ID3v2 ratings are the POPM frame, written after setProperties.
            if (!id3)
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
    if (id3 && ratingTouched) {
        const bool create = !after.first(QStringLiteral("Rating")).isEmpty();
        if (TagLib::ID3v2::Tag *tag = id3v2TagFor(file, create))
            rewritePopm(tag, keep, after);
    }
    return id3 ? saveId3(file, options) : file->save();
}

}

WriteResult writeTags(
    const QString &filePath, const TagChanges &changes, const WriteOptions &options)
{
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
        saved = saveChanges(filePath, read.tags, checked, after, options);
    } catch (...) {
        // writeTags never throws; a tag library failure is Failed.
        saved = false;
    }
    result.status = saved ? WriteResult::Status::Ok : WriteResult::Status::Failed;
    return result;
}

}
