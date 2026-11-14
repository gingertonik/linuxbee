#include "tagwriter.h"

#include <algorithm>
#include <memory>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <fileref.h>
#include <id3v2.h>
#include <id3v2tag.h>
#include <mpegfile.h>
#include <popularimeterframe.h>
#include <tpropertymap.h>
#include <tstringlist.h>
#include <tvariant.h>
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

// W-PICTURES: the file's pictures become exactly this list, in order. TagLib
// writes each one under the PICTURE key of its format.
bool writePictures(TagLib::File *file, const QList<Picture> &pictures)
{
    TagLib::List<TagLib::VariantMap> list;
    for (const Picture &picture : pictures) {
        TagLib::VariantMap map;
        map.insert(TagLib::String("data"),
            TagLib::Variant(TagLib::ByteVector(
                picture.data.constData(), static_cast<unsigned int>(picture.data.size()))));
        map.insert(TagLib::String("mimeType"), TagLib::Variant(tagString(picture.mimeType)));
        map.insert(TagLib::String("description"), TagLib::Variant(tagString(picture.description)));
        map.insert(TagLib::String("pictureType"), TagLib::Variant(tagString(picture.type)));
        list.append(map);
    }
    return file->setComplexProperties(TagLib::String("PICTURE"), list);
}

// Step 5 of spec section 1: edits the property map, then saves. The pictures are
// written only when the change names them.
bool saveChanges(const QString &filePath, const TrackTags &now, const tags::CheckedChanges &checked,
    const TrackTags &after, const WriteOptions &options, bool picturesChanged)
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
    if (picturesChanged && !writePictures(file, after.pictures))
        return false;
    if (id3 && ratingTouched) {
        const bool create = !after.first(QStringLiteral("Rating")).isEmpty();
        if (TagLib::ID3v2::Tag *tag = id3v2TagFor(file, create))
            rewritePopm(tag, keep, after);
    }
    return id3 ? saveId3(file, options) : file->save();
}

// WS-NAME: the new version of the file sits beside it as ".lbsave-" + six
// characters chosen at random from a-z and 0-9 + "-" + the file's own name.
// The name ends with the file's name so the extension, which tells the tag
// library the format, is kept; it begins with a dot so it is not shown.
QString scratchName(const QString &fileName)
{
    static constexpr char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    QString chosen;
    for (int i = 0; i < 6; ++i)
        chosen
            += QLatin1Char(alphabet[QRandomGenerator::global()->bounded(int(sizeof alphabet) - 1)]);
    return QStringLiteral(".lbsave-") + chosen + QLatin1Char('-') + fileName;
}

// POSIX: force a saved file's contents, or a folder's entries, to the disk. False when
// the path cannot be opened for it or fsync fails (WS-SYNC). The descriptor is closed
// on both paths.
bool syncToDisk(const QString &path, bool isFolder)
{
    const int fd
        = ::open(QFile::encodeName(path).constData(), O_RDONLY | (isFolder ? O_DIRECTORY : 0));
    if (fd < 0)
        return false;
    const bool synced = ::fsync(fd) == 0;
    ::close(fd);
    return synced;
}

// WS-KILLED: a process that died during an earlier save of this file left its copy
// behind. Step 1 of WS-ORDER removes every file in the folder shaped exactly like
// this file's copies, and nothing else. The names are compared by hand on purpose:
// a music file's name carries [ ] * ? , which a name filter takes for wildcards.
void clearLeftovers(const QString &folder, const QString &fileName)
{
    const QString head = QStringLiteral(".lbsave-");
    const QString tail = QLatin1Char('-') + fileName;
    const QStringList names = QDir(folder).entryList(QDir::Files | QDir::Hidden, QDir::Name);
    for (const QString &name : names) {
        if (name.size() != head.size() + 6 + tail.size() || !name.startsWith(head)
            || !name.endsWith(tail))
            continue;
        const QString chosen = name.mid(head.size(), 6);
        if (!std::all_of(chosen.cbegin(), chosen.cend(), [](QChar c) {
                return (c.unicode() >= u'a' && c.unicode() <= u'z')
                    || (c.unicode() >= u'0' && c.unicode() <= u'9');
            }))
            continue;
        QFile::remove(folder + QLatin1Char('/') + name);
    }
}

// WS-KEEP: the new version has the original's permission bits, its owner and group
// where the system allows that, and its extended attributes. An ordinary user cannot
// take another's owner and cannot write every attribute namespace: those refusals are
// the file system's, not the save's, so the copy becomes the user's own and the save
// goes on. The times are set by W-MTIME after this, being the last thing written.
void giveWhatTheOriginalHad(const QString &copyPath, const QString &original)
{
    const QByteArray copyName = QFile::encodeName(copyPath);
    const QByteArray originalName = QFile::encodeName(original);
    struct stat had { };
    if (::stat(originalName.constData(), &had) != 0)
        return;
    if (::chmod(copyName.constData(), had.st_mode & 07777) != 0)
        return;
    if (::chown(copyName.constData(), had.st_uid, had.st_gid) != 0) {
        // The file stays the user's own; WS-KEEP says the save still succeeds.
    }

    const ssize_t listed = ::listxattr(originalName.constData(), nullptr, 0);
    if (listed <= 0)
        return;
    QByteArray names(int(listed), '\0');
    const ssize_t got = ::listxattr(originalName.constData(), names.data(), names.size());
    if (got <= 0)
        return;

    // The names come back as one buffer of zero-ended strings.
    const char *cursor = names.constData();
    const char *const end = cursor + got;
    while (cursor < end) {
        const QByteArray attribute(cursor);
        const ssize_t wanted
            = ::getxattr(originalName.constData(), attribute.constData(), nullptr, 0);
        if (wanted >= 0) {
            QByteArray value(int(wanted), '\0');
            const ssize_t bytes = ::getxattr(
                originalName.constData(), attribute.constData(), value.data(), value.size());
            if (bytes >= 0)
                (void)::setxattr(copyName.constData(), attribute.constData(), value.constData(),
                    size_t(bytes), 0);
        }
        cursor += attribute.size() + 1;
    }
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

    // WS-REFUSED: the file itself may not be written, though replacing it would be
    // possible: a file marked read-only is to be left alone.
    if (!QFileInfo(filePath).isWritable()) {
        result.status = WriteResult::Status::NotWritable;
        return result;
    }

    // WS-LINK: the file the path leads to is the one saved, in its own folder.
    const QString target = QFileInfo(filePath).canonicalFilePath();
    if (target.isEmpty()) {
        result.status = WriteResult::Status::Failed;
        return result;
    }
    const QFileInfo targetInfo(target);
    const QString folder = targetInfo.absolutePath();

    // WS-REFUSED: and so is a file whose folder takes no new file. The check is here,
    // before the copy, so that refusing creates nothing at all.
    if (!QFileInfo(folder).isWritable()) {
        result.status = WriteResult::Status::NotWritable;
        return result;
    }

    const QDateTime modified = targetInfo.lastModified();

    // WS-ORDER 1, WS-KILLED: clear up after a save of this file that died half-way,
    // so the copy made below is the only new file in the folder.
    clearLeftovers(folder, targetInfo.fileName());

    // WS-ORDER 2, WS-NAME: a whole new version of the file beside the original.
    QString copyPath;
    do
        copyPath = folder + QLatin1Char('/') + scratchName(targetInfo.fileName());
    while (QFile::exists(copyPath));
    if (!QFile::copy(target, copyPath)) {
        result.status = WriteResult::Status::Failed;
        return result;
    }

    // WS-ORDER 3: the changes are saved into the copy. Nothing of the original is
    // opened for writing, so until step 6 it is byte for byte what it was.
    bool saved = false;
    try {
        saved = saveChanges(
            copyPath, read.tags, checked, after, options, changes.pictures.has_value());
    } catch (...) {
        // writeTags never throws; a tag library failure is Failed.
        saved = false;
    }
    if (!saved) {
        QFile::remove(copyPath);
        result.status = WriteResult::Status::Failed;
        return result;
    }

    // WS-KEEP: the copy takes what the original had: permission bits, owner where
    // the system allows, extended attributes.
    giveWhatTheOriginalHad(copyPath, target);

    // W-MTIME: with keepModificationTime the copy gets the original's time, and the
    // time goes on last, so nothing written afterwards moves it. Without it the copy
    // keeps the time of the save.
    if (options.keepModificationTime) {
        QFile copy(copyPath);
        if (copy.open(QIODevice::ReadOnly))
            copy.setFileTime(modified, QFileDevice::FileModificationTime);
    }

    // WS-ORDER 4: the copy's contents are forced to the disk before the swap. WS-SYNC:
    // when that fails the copy goes, the original stays, and the hook is not called.
    if (!syncToDisk(copyPath, false)) {
        QFile::remove(copyPath);
        result.status = WriteResult::Status::Failed;
        return result;
    }

    // WS-HOOK: the last moment to stop a save, with both versions complete.
    if (options.beforeReplace && !options.beforeReplace(copyPath)) {
        QFile::remove(copyPath);
        result.status = WriteResult::Status::Failed;
        return result;
    }

    // WS-ORDER 6: rename(2) puts the copy in the original's place in one step, so
    // the path never holds half a file. Then the folder's entries are forced too.
    if (::rename(QFile::encodeName(copyPath).constData(), QFile::encodeName(target).constData())
        != 0) {
        QFile::remove(copyPath);
        result.status = WriteResult::Status::Failed;
        return result;
    }
    // WS-SYNC: the new version is already in place, so a folder that cannot be forced
    // is reported as Failed and nothing is removed or renamed.
    if (!syncToDisk(folder, true)) {
        result.status = WriteResult::Status::Failed;
        return result;
    }

    result.status = WriteResult::Status::Ok;
    return result;
}

}
