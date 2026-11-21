#include "tagwriter.h"

#include <algorithm>
#include <cerrno>
#include <memory>
#include <vector>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>

#include <fcntl.h>
#include <stdio.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <commentsframe.h>
#include <fileref.h>
#include <id3v1tag.h>
#include <id3v2.h>
#include <id3v2frame.h>
#include <id3v2framefactory.h>
#include <id3v2header.h>
#include <id3v2tag.h>
#include <mp4file.h>
#include <mp4item.h>
#include <mp4itemfactory.h>
#include <mp4tag.h>
#include <mpegfile.h>
#include <popularimeterframe.h>
#include <textidentificationframe.h>
#include <tiostream.h>
#include <tpropertymap.h>
#include <tstringlist.h>
#include <tvariant.h>
#include <unsynchronizedlyricsframe.h>
#include <wavfile.h>
#include <wavpackfile.h>
#include <apeitem.h>
#include <apetag.h>

#include "catalogue.h"
#include "changes.h"
#include "tagreader.h"

namespace linuxbee {

namespace {

using tags::KeyFamily;

// W-TABLE: the family decides the keys. MP3 and WAV carry ID3v2; FLAC, Ogg, Opus
// and Speex carry Vorbis comments; MPEG-4 and WavPack carry atoms and APEv2 items.
KeyFamily familyOf(const QString &format)
{
    if (format == QLatin1String("mp3") || format == QLatin1String("wav"))
        return KeyFamily::Id3v2;
    if (format == QLatin1String("flac") || format == QLatin1String("ogg")
        || format == QLatin1String("opus") || format == QLatin1String("spx"))
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
    // W-DISCTOTAL: a Vorbis DISCTOTAL the file has is kept, with the count's value.
    const bool hadDiscTotal = family == KeyFamily::Vorbis
        && totalField == QLatin1String("Disc Count")
        && map.contains(tagString(QStringLiteral("DISCTOTAL")));
    for (const QString &key : tags::readKeys(numberField, family))
        map.erase(tagString(key));
    for (const QString &key : tags::readKeys(totalField, family))
        map.erase(tagString(key));

    const QString number = after.first(numberField);
    const QString total = after.first(totalField);
    if (family == KeyFamily::Vorbis) {
        if (!number.isEmpty())
            map.insert(tagString(tags::writeKey(numberField, family)), tagList({ number }));
        if (!total.isEmpty()) {
            map.insert(tagString(tags::writeKey(totalField, family)), tagList({ total }));
            if (hadDiscTotal)
                map.insert(tagString(QStringLiteral("DISCTOTAL")), tagList({ total }));
        }
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

// W-ID3VER, Keep: the major version of the ID3v2 tag the file has now, read from the
// tag's own header before anything is changed. Only an ID3v2.3 tag gives 3; no tag, or any
// other version, gives 4. Only MP3 and WAV are asked.
TagLib::ID3v2::Version versionOfFile(TagLib::File *file)
{
    const TagLib::ID3v2::Tag *tag = nullptr;
    if (auto *mpeg = dynamic_cast<TagLib::MPEG::File *>(file)) {
        if (mpeg->hasID3v2Tag())
            tag = mpeg->ID3v2Tag();
    } else if (auto *wav = dynamic_cast<TagLib::RIFF::WAV::File *>(file)) {
        if (wav->hasID3v2Tag())
            tag = wav->ID3v2Tag();
    }
    if (tag && tag->header() && tag->header()->majorVersion() == 3)
        return TagLib::ID3v2::v3;
    return TagLib::ID3v2::v4;
}

// W-ID3VER: the ID3v2 version a save writes. `found` is what the file had (Keep).
TagLib::ID3v2::Version id3Version(const WriteOptions &options, TagLib::ID3v2::Version found)
{
    switch (options.id3Version) {
    case WriteOptions::Id3Version::V23:
        return TagLib::ID3v2::v3;
    case WriteOptions::Id3Version::V24:
        return TagLib::ID3v2::v4;
    case WriteOptions::Id3Version::Keep:
        break;
    }
    return found;
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

// W-RATING, ID3v2: the rating is one byte of the first POPM frame. Its address and counter,
// and every other POPM frame, are not touched. A file with no POPM frame gets one with an
// empty address and a counter of 0. A rating taken out sets the byte to 0 and leaves the
// frame, unless it is the only POPM frame and its counter is 0: then it goes.
void rewritePopm(TagLib::ID3v2::Tag *tag, const TrackTags &after)
{
    bool parsed = false;
    const double stars = after.first(QStringLiteral("Rating")).toDouble(&parsed);
    const int byte = parsed ? popmFromRating(stars) : 0;
    const TagLib::ID3v2::FrameList frames = tag->frameList(TagLib::ByteVector("POPM"));
    auto *first = frames.isEmpty()
        ? nullptr
        : dynamic_cast<TagLib::ID3v2::PopularimeterFrame *>(frames.front());
    if (first) {
        if (byte != 0 || first->counter() > 0 || frames.size() > 1) {
            first->setRating(byte);
        } else {
            tag->removeFrame(first, true);
        }
        return;
    }
    if (byte == 0)
        return;
    auto frame = std::make_unique<TagLib::ID3v2::PopularimeterFrame>();
    frame->setRating(byte);
    tag->addFrame(frame.release());
}

// W-RATING, "the frames whole": the tag's POPM frames, in the order they stand.
QList<RatingFrame> ratingFramesOf(const TagLib::ID3v2::Tag *tag)
{
    QList<RatingFrame> found;
    if (!tag)
        return found;
    const TagLib::ID3v2::FrameList frames = tag->frameList(TagLib::ByteVector("POPM"));
    for (const TagLib::ID3v2::Frame *frame : frames) {
        if (const auto *popm = dynamic_cast<const TagLib::ID3v2::PopularimeterFrame *>(frame)) {
            found.append({ QString::fromUtf8(popm->email().toCString(true)), popm->rating(),
                quint32(popm->counter()) });
        }
    }
    return found;
}

// W-RATING, "the frames whole": the tag's POPM frames become exactly these, in order.
void putRatingFrames(TagLib::ID3v2::Tag *tag, const QList<RatingFrame> &frames)
{
    tag->removeFrames(TagLib::ByteVector("POPM"));
    for (const RatingFrame &kept : frames) {
        auto frame = std::make_unique<TagLib::ID3v2::PopularimeterFrame>();
        frame->setEmail(tagString(kept.address));
        frame->setRating(kept.rating);
        frame->setCounter(kept.counter);
        tag->addFrame(frame.release());
    }
}

// W-ID3V1, W-ID3VER, W-WAV: how an ID3 file is saved. Other families use TagLib's
// default save.
bool saveId3(TagLib::File *file, const WriteOptions &options, TagLib::ID3v2::Version version)
{
    if (auto *mpeg = dynamic_cast<TagLib::MPEG::File *>(file)) {
        int kinds = TagLib::MPEG::File::ID3v2;
        if (options.writeId3v1)
            kinds |= TagLib::MPEG::File::ID3v1;
        // An APEv2 tag is not part of this change: it stays if the file has one.
        if (mpeg->hasAPETag())
            kinds |= TagLib::MPEG::File::APE;
        return mpeg->save(kinds, TagLib::File::StripOthers, version,
            options.writeId3v1 ? TagLib::File::Duplicate : TagLib::File::DoNotDuplicate);
    }
    if (auto *wav = dynamic_cast<TagLib::RIFF::WAV::File *>(file)) {
        // W-WAV: the ID3 chunk only. No RIFF Info chunk is created, and none is stripped.
        return wav->save(TagLib::RIFF::WAV::File::ID3v2, TagLib::File::StripNone, version);
    }
    return file->save();
}

// W-PICTURES: the file's pictures become exactly this list, in order. TagLib
// writes each one under the PICTURE key of its format. `target` is the file, or, for an ID3
// file, its ID3v2 tag alone (the file would write them into an APEv2 tag as well).
template <class Target> bool writePictures(Target *target, const QList<Picture> &pictures)
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
    return target->setComplexProperties(TagLib::String("PICTURE"), list);
}

// W-ONLY: Frame::asProperties is protected in the tag library. Naming it through a derived
// struct lets a pointer to member reach it on any frame, and the call still goes to the
// frame's own override: these are the keys the tag library reads that frame as.
struct FrameProperties : TagLib::ID3v2::Frame {
    static TagLib::PropertyMap of(const TagLib::ID3v2::Frame *frame)
    {
        return (frame->*(&FrameProperties::asProperties))();
    }
};

TagLib::StringList valuesOf(const TagLib::PropertyMap &map, const TagLib::String &key)
{
    const auto found = map.find(key);
    return found == map.end() ? TagLib::StringList() : found->second;
}

// W-ONLY: a key is named when the edited map holds other values for it than the map as it was
// read: a new key, a key that is gone, or other values.
TagLib::StringList namedKeys(const TagLib::PropertyMap &was, const TagLib::PropertyMap &now)
{
    TagLib::StringList named;
    auto consider = [&](const TagLib::String &key) {
        if (!named.contains(key) && valuesOf(was, key) != valuesOf(now, key))
            named.append(key);
    };
    for (const auto &entry : was)
        consider(entry.first);
    for (const auto &entry : now)
        consider(entry.first);
    return named;
}

// The tag library's String has an upper() and no lower(). Roles are written in small letters,
// as the files that are read hold them.
TagLib::String lowerOf(const TagLib::String &text)
{
    return tagString(QString::fromStdString(text.to8Bit(true)).toLower());
}

// W-ONLY, lists of people: TIPL holds the keys of involvedPeopleMap(), TMCL the keys that begin
// PERFORMER:, each as a pair of a role and a name. False for any other key.
bool listOfPeopleFor(const TagLib::String &key, TagLib::ByteVector &frameId, TagLib::String &role)
{
    const auto &people = TagLib::ID3v2::TextIdentificationFrame::involvedPeopleMap();
    const auto found = people.find(key);
    if (found != people.end()) {
        frameId = TagLib::ByteVector("TIPL");
        role = lowerOf(found->second);
        return true;
    }
    if (key.startsWith(TagLib::String("PERFORMER:"))) {
        frameId = TagLib::ByteVector("TMCL");
        role = lowerOf(key.substr(10));
        return true;
    }
    return false;
}

// W-ONLY, lists of people: the pairs of the named role (compared ignoring case) are taken out
// of the list's frame and the new ones appended; every other pair stays, in its order. A list
// left with no pair is removed, and a missing list is made.
void rewriteListOfPeople(TagLib::ID3v2::Tag *tag, const TagLib::ByteVector &frameId,
    const TagLib::String &role, const TagLib::StringList &values)
{
    TagLib::ID3v2::TextIdentificationFrame *frame = nullptr;
    const TagLib::ID3v2::FrameList held = tag->frameList(frameId);
    if (!held.isEmpty())
        frame = dynamic_cast<TagLib::ID3v2::TextIdentificationFrame *>(held.front());

    TagLib::StringList kept;
    if (frame) {
        const TagLib::StringList fields = frame->fieldList();
        for (unsigned int i = 0; i < fields.size(); i += 2) {
            // An odd last text has no name beside it: it is nobody's pair, and stays.
            if (i + 1 < fields.size() && fields[i].upper() == role.upper())
                continue;
            kept.append(fields[i]);
            if (i + 1 < fields.size())
                kept.append(fields[i + 1]);
        }
    }
    for (const TagLib::String &value : values) {
        kept.append(role);
        kept.append(value);
    }

    if (kept.isEmpty()) {
        if (frame)
            tag->removeFrame(frame, true);
        return;
    }
    if (!frame) {
        frame = new TagLib::ID3v2::TextIdentificationFrame(frameId, TagLib::String::UTF8);
        tag->addFrame(frame);
    }
    frame->setText(kept);
}

enum class TextKind { Other, Comment, Lyrics };

// W-ONLY: the keys the tag library writes as a comment frame or a lyrics frame, a description
// after a colon included.
TextKind textKindOf(const TagLib::String &key)
{
    if (key == "COMMENT" || key.startsWith(TagLib::String("COMMENT:")))
        return TextKind::Comment;
    if (key == "LYRICS" || key.startsWith(TagLib::String("LYRICS:")))
        return TextKind::Lyrics;
    return TextKind::Other;
}

// The language and the description of a comment frame or a lyrics frame that is being replaced.
struct Replaced {
    TagLib::ByteVector language;
    TagLib::String description;
};

// W-ONLY, named: the frames of the key are taken out, and the new values are written as the tag
// library writes that key. Comments and lyrics are one frame a value (the library would make
// several into one user text frame), and a value gets a frame of its own only where it takes
// the place of a frame that was there (language and description, alike counting once); the
// values beyond the places are added to the last frame written, after a line feed.
void writeNamedKey(
    TagLib::ID3v2::Tag *tag, const TagLib::String &key, const TagLib::StringList &values)
{
    TagLib::ByteVector listId;
    TagLib::String role;
    if (listOfPeopleFor(key, listId, role)) {
        rewriteListOfPeople(tag, listId, role, values);
        // The key may be held in another frame too (some taggers write an arranger or a
        // producer as a user text frame): that frame is a frame of the key, and is taken
        // out. The value is in the list alone.
        std::vector<TagLib::ID3v2::Frame *> others;
        for (TagLib::ID3v2::Frame *frame : tag->frameList()) {
            const TagLib::ByteVector id = frame->frameID();
            if (id == "TIPL" || id == "TMCL" || id == "IPLS")
                continue;
            if (FrameProperties::of(frame).contains(key))
                others.push_back(frame);
        }
        for (TagLib::ID3v2::Frame *frame : others)
            tag->removeFrame(frame, true);
        return;
    }

    const TextKind kind = textKindOf(key);
    std::vector<TagLib::ID3v2::Frame *> mine;
    for (TagLib::ID3v2::Frame *frame : tag->frameList()) {
        if (FrameProperties::of(frame).contains(key))
            mine.push_back(frame);
    }
    std::vector<Replaced> replaced;
    for (TagLib::ID3v2::Frame *frame : mine) {
        if (auto *comment = dynamic_cast<TagLib::ID3v2::CommentsFrame *>(frame))
            replaced.push_back({ comment->language(), comment->description() });
        else if (auto *lyrics = dynamic_cast<TagLib::ID3v2::UnsynchronizedLyricsFrame *>(frame))
            replaced.push_back({ lyrics->language(), lyrics->description() });
        tag->removeFrame(frame, true);
    }
    if (values.isEmpty())
        return;

    TagLib::ID3v2::FrameFactory *factory = TagLib::ID3v2::FrameFactory::instance();
    if (kind == TextKind::Other) {
        if (TagLib::ID3v2::Frame *frame = factory->createFrameForProperty(key, values))
            tag->addFrame(frame);
        return;
    }
    // The places are the language and description of each frame replaced, in order, two alike
    // counting once. Value n goes to place n; the values beyond the places are added to the
    // last frame written, after a line feed. With no frame replaced there is one place, the
    // frame as the tag library makes it.
    std::vector<Replaced> places;
    for (const Replaced &from : replaced) {
        bool seen = false;
        for (const Replaced &place : places)
            seen = seen
                || (place.language == from.language && place.description == from.description);
        if (!seen)
            places.push_back(from);
    }
    const size_t count = places.empty() ? 1 : places.size();
    for (size_t at = 0; at < count && at < values.size(); ++at) {
        TagLib::String text = values[static_cast<unsigned int>(at)];
        if (at + 1 == count) {
            for (size_t more = at + 1; more < values.size(); ++more) {
                text += TagLib::String("\n");
                text += values[static_cast<unsigned int>(more)];
            }
        }
        TagLib::StringList one;
        one.append(text);
        TagLib::ID3v2::Frame *frame = factory->createFrameForProperty(key, one);
        if (!frame)
            continue;
        if (!places.empty()) {
            const Replaced &from = places[at];
            if (auto *comment = dynamic_cast<TagLib::ID3v2::CommentsFrame *>(frame)) {
                comment->setLanguage(from.language);
                comment->setDescription(from.description);
            } else if (auto *lyrics
                = dynamic_cast<TagLib::ID3v2::UnsynchronizedLyricsFrame *>(frame)) {
                lyrics->setLanguage(from.language);
                lyrics->setDescription(from.description);
            }
        }
        tag->addFrame(frame);
    }
}

// W-ONLY, for an ID3 file (MP3, WAV): instead of handing the tag library the whole map to
// write anew, only the frames of the keys the change names are rewritten. `was` is the map as
// it was read, `now` the map the change makes. The ID3v1 tag, which the library's
// setProperties used to set in the same call, is set from `now` (W-ID3V1).
void saveId3Properties(TagLib::File *file, TagLib::ID3v2::Tag *tag, const TagLib::PropertyMap &was,
    const TagLib::PropertyMap &now)
{
    TagLib::StringList named = namedKeys(was, now);

    // A value the file was read as having that the ID3v2 tag has no frame for came from another
    // tag (APEv2, or ID3v1): it is written into the ID3v2 tag too, named or not, or it would no
    // longer be shown once the file is read from the ID3v2 tag.
    auto *mpeg = dynamic_cast<TagLib::MPEG::File *>(file);
    {
        TagLib::StringList heldKeys;
        for (const TagLib::ID3v2::Frame *frame : tag->frameList()) {
            for (const auto &entry : FrameProperties::of(frame))
                heldKeys.append(entry.first);
        }
        for (const auto &entry : now) {
            const TagLib::String &key = entry.first;
            if (!named.contains(key) && !heldKeys.contains(key) && !entry.second.isEmpty())
                named.append(key);
        }
    }

    for (const TagLib::String &key : named)
        writeNamedKey(tag, key, valuesOf(now, key));

    if (mpeg)
        mpeg->ID3v1Tag(true)->setProperties(now);
}

// W-ONLY, MPEG-4: the factory takes and gives item names as bytes, the tag's map has them as
// strings. A freeform name (----:mean:name) is UTF-8; an atom's four letters are Latin-1 (the
// first may be a copyright sign, one byte).
TagLib::ByteVector itemNameBytes(const TagLib::String &name)
{
    return name.data(
        name.startsWith(TagLib::String("----:")) ? TagLib::String::UTF8 : TagLib::String::Latin1);
}

TagLib::String itemNameString(const TagLib::ByteVector &name)
{
    return TagLib::String(name,
        name.startsWith(TagLib::ByteVector("----:")) ? TagLib::String::UTF8
                                                     : TagLib::String::Latin1);
}

// W-ONLY, MPEG-4, named: every item the tag library reads this key from (compared without case)
// is taken out, and one item is written for the new values, under the name the factory gives;
// but where an item taken out had that name in other letters, under its own name. When the
// factory cannot make an item for the values, the key's items stay as they are.
void writeNamedMp4Key(
    TagLib::MP4::Tag *tag, const TagLib::String &key, const TagLib::StringList &values)
{
    const TagLib::MP4::ItemFactory *factory = TagLib::MP4::ItemFactory::instance();

    std::pair<TagLib::ByteVector, TagLib::MP4::Item> made;
    if (!values.isEmpty()) {
        made = factory->itemFromProperty(key, values);
        if (made.first.isEmpty() || !made.second.isValid())
            return;
    }

    std::vector<TagLib::String> mine;
    for (const auto &entry : tag->itemMap()) {
        const auto read = factory->itemToProperty(itemNameBytes(entry.first), entry.second);
        if (!read.first.isEmpty() && read.first.upper() == key.upper())
            mine.push_back(entry.first);
    }
    for (const TagLib::String &name : mine)
        tag->removeItem(name);
    if (values.isEmpty())
        return;

    TagLib::String name = itemNameString(made.first);
    for (const TagLib::String &was : mine) {
        if (was.upper() == name.upper()) {
            name = was;
            break;
        }
    }
    tag->setItem(name, made.second);
}

// W-ONLY, for an MPEG-4 file: instead of handing the tag library the whole map to write anew,
// only the items of the keys the change names are rewritten.
void saveMp4Properties(
    TagLib::MP4::Tag *tag, const TagLib::PropertyMap &was, const TagLib::PropertyMap &now)
{
    for (const TagLib::String &key : namedKeys(was, now))
        writeNamedMp4Key(tag, key, valuesOf(now, key));
}

// WS-WRITTEN: a file the tag library writes through, byte for byte and with no buffer of its
// own. Every write is looped until its last byte is down, and one that is not is remembered in
// `failed()`; the tag library is not believed about writes. The library's own insert and remove
// move the rest of a file with writes that no override of a write call sees, so both are done
// here, through the same checked writes.
class CheckedStream : public TagLib::IOStream {
public:
    explicit CheckedStream(const QString &filePath)
        : m_name(QFile::encodeName(filePath))
        , m_fd(::open(m_name.constData(), O_RDWR | O_CLOEXEC))
    {
    }
    ~CheckedStream() override
    {
        if (m_fd >= 0)
            ::close(m_fd);
    }

    bool failed() const { return m_failed; }

    TagLib::FileName name() const override { return m_name.constData(); }

    TagLib::ByteVector readBlock(size_t wanted) override
    {
        TagLib::ByteVector data;
        if (m_fd < 0 || wanted == 0)
            return data;
        // A block longer than one step is cut to what the file still has after the position,
        // before the buffer is sized, as the tag library's own stream does: a tag header that
        // claims 256 MB must not make a save allocate 256 MB to read a few kilobytes.
        if (wanted > kStep) {
            const TagLib::offset_t size = length();
            const TagLib::offset_t left = size > m_pos ? size - m_pos : 0;
            if (static_cast<TagLib::offset_t>(wanted) > left)
                wanted = static_cast<size_t>(left);
            if (wanted == 0)
                return data;
        }
        data.resize(static_cast<unsigned int>(wanted));
        const size_t got = readAt(data.data(), wanted, m_pos);
        data.resize(static_cast<unsigned int>(got));
        m_pos += static_cast<TagLib::offset_t>(got);
        return data;
    }

    void writeBlock(const TagLib::ByteVector &data) override
    {
        if (!writeAt(data.data(), data.size(), m_pos))
            return;
        m_pos += static_cast<TagLib::offset_t>(data.size());
    }

    void insert(const TagLib::ByteVector &data, TagLib::offset_t start, size_t replace) override
    {
        if (m_fd < 0) {
            m_failed = true;
            return;
        }
        const TagLib::offset_t end = length();
        const TagLib::offset_t put = static_cast<TagLib::offset_t>(data.size());
        if (start < 0 || start >= end) {
            m_pos = std::max<TagLib::offset_t>(start, 0);
            writeBlock(data);
            return;
        }
        const TagLib::offset_t cut
            = std::min<TagLib::offset_t>(static_cast<TagLib::offset_t>(replace), end - start);
        const TagLib::offset_t tail = start + cut;
        const TagLib::offset_t moveTo = start + put;
        if (put > cut)
            moveBackwards(tail, end, moveTo);
        else if (put < cut)
            moveForwards(tail, end, moveTo);
        if (put < cut && !m_failed)
            truncate(end - (cut - put));
        if (m_failed)
            return;
        m_pos = start;
        writeBlock(data);
    }

    void removeBlock(TagLib::offset_t start, size_t length) override
    {
        insert(TagLib::ByteVector(), start, length);
    }

    bool readOnly() const override { return false; }
    bool isOpen() const override { return m_fd >= 0; }

    void seek(TagLib::offset_t offset, Position p) override
    {
        TagLib::offset_t to = offset;
        if (p == Current)
            to = m_pos + offset;
        else if (p == End)
            to = length() + offset;
        if (to >= 0)
            m_pos = to;
    }

    TagLib::offset_t tell() const override { return m_pos; }

    TagLib::offset_t length() override
    {
        struct stat info { };
        if (m_fd < 0 || ::fstat(m_fd, &info) != 0) {
            m_failed = true;
            return 0;
        }
        return static_cast<TagLib::offset_t>(info.st_size);
    }

    void truncate(TagLib::offset_t length) override
    {
        if (m_fd < 0 || length < 0 || ::ftruncate(m_fd, static_cast<off_t>(length)) != 0)
            m_failed = true;
    }

private:
    static constexpr size_t kStep = 1024 * 1024;

    // Up to `size` bytes from `at`; fewer only at the end of the file. A read that fails is a
    // failure of the stream.
    size_t readAt(char *into, size_t size, TagLib::offset_t at)
    {
        size_t done = 0;
        while (done < size) {
            const ssize_t got
                = ::pread(m_fd, into + done, size - done, static_cast<off_t>(at) + off_t(done));
            if (got < 0 && errno == EINTR)
                continue;
            if (got < 0)
                m_failed = true;
            if (got <= 0)
                break;
            done += size_t(got);
        }
        return done;
    }

    // All of `size` bytes at `at`, or false and the failure remembered.
    bool writeAt(const char *from, size_t size, TagLib::offset_t at)
    {
        if (m_fd < 0) {
            m_failed = true;
            return false;
        }
        size_t done = 0;
        while (done < size) {
            const ssize_t put
                = ::pwrite(m_fd, from + done, size - done, static_cast<off_t>(at) + off_t(done));
            if (put < 0 && errno == EINTR)
                continue;
            if (put <= 0) {
                m_failed = true;
                return false;
            }
            done += size_t(put);
        }
        return true;
    }

    // [from, end) moves to `to` (later in the file), the last block first, so nothing is
    // written over before it has been read.
    void moveBackwards(TagLib::offset_t from, TagLib::offset_t end, TagLib::offset_t to)
    {
        QByteArray block(int(kStep), '\0');
        TagLib::offset_t top = end;
        while (top > from && !m_failed) {
            const size_t n
                = size_t(std::min<TagLib::offset_t>(top - from, TagLib::offset_t(kStep)));
            const TagLib::offset_t at = top - TagLib::offset_t(n);
            if (readAt(block.data(), n, at) != n) {
                m_failed = true; // a block that was to be read whole was not
                return;
            }
            writeAt(block.constData(), n, at + (to - from));
            top = at;
        }
    }

    // [from, end) moves to `to` (earlier in the file), the first block first.
    void moveForwards(TagLib::offset_t from, TagLib::offset_t end, TagLib::offset_t to)
    {
        QByteArray block(int(kStep), '\0');
        TagLib::offset_t at = from;
        while (at < end && !m_failed) {
            const size_t n = size_t(std::min<TagLib::offset_t>(end - at, TagLib::offset_t(kStep)));
            if (readAt(block.data(), n, at) != n) {
                m_failed = true;
                return;
            }
            writeAt(block.constData(), n, at - (from - to));
            at += TagLib::offset_t(n);
        }
    }

    QByteArray m_name;
    int m_fd = -1;
    TagLib::offset_t m_pos = 0;
    bool m_failed = false;
};

// The names an APEv2 tag's items have in the file, by the capitals the tag's map is keyed in.
using ApeNames = std::vector<std::pair<TagLib::String, TagLib::String>>;

ApeNames apeNamesOf(TagLib::File *file)
{
    ApeNames names;
    auto *wavpack = dynamic_cast<TagLib::WavPack::File *>(file);
    if (!wavpack || !wavpack->hasAPETag())
        return names;
    const TagLib::APE::ItemListMap &items = wavpack->APETag()->itemListMap();
    for (auto it = items.begin(); it != items.end(); ++it)
        names.emplace_back(it->first, it->second.key());
    return names;
}

// Every item the file had, written anew under capitals, gets the file's own spelling back.
// A key the file did not have stays as written.
void restoreApeNames(TagLib::File *file, const ApeNames &names)
{
    auto *wavpack = dynamic_cast<TagLib::WavPack::File *>(file);
    if (!wavpack || names.empty() || !wavpack->hasAPETag())
        return;
    TagLib::APE::Tag *tag = wavpack->APETag();
    for (const auto &name : names) {
        const TagLib::APE::ItemListMap &items = tag->itemListMap();
        const auto found = items.find(name.first);
        if (found == items.end() || found->second.key() == name.second)
            continue;
        TagLib::APE::Item item = found->second;
        item.setKey(name.second);
        tag->setItem(name.first, item);
    }
}

// Step 5 of spec section 1: edits the property map, then saves. The pictures are
// written only when the change names them. An ID3 file is not given the map: only the frames
// of the keys the edit changed are rewritten (W-ONLY).
// `wroteV23` is set when the tag written is ID3v2.3 (W-ID3VER, not kept).
// WS-WRITTEN: false too when any write into the copy did not put down all its bytes.
bool saveChanges(const QString &filePath, const TrackTags &now, const tags::CheckedChanges &checked,
    const TrackTags &after, const WriteOptions &options, bool picturesChanged,
    const std::optional<QList<RatingFrame>> &ratingFrames, bool &wroteV23)
{
    wroteV23 = false;
    const KeyFamily family = familyOf(now.format);
    // The stream is declared first: it outlives the FileRef that writes through it.
    CheckedStream stream(filePath);
    if (!stream.isOpen())
        return false;
    TagLib::FileRef ref(&stream, false);
    if (ref.isNull() || !ref.file() || !ref.file()->isValid())
        return false;
    TagLib::File *file = ref.file();

    const bool id3 = family == KeyFamily::Id3v2;
    // W-ID3VER: the file's own version, found before any property is set.
    const TagLib::ID3v2::Version version
        = id3 ? id3Version(options, versionOfFile(file)) : TagLib::ID3v2::v4;
    wroteV23 = id3 && version == TagLib::ID3v2::v3;
    const bool ratingTouched = checked.fields.contains(QStringLiteral("Rating"));
    TagLib::PropertyMap map = file->properties();
    // W-ONLY: the map as it was read, to tell which keys the change names.
    const TagLib::PropertyMap was = map;

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

    if (id3) {
        // W-ONLY: no setProperties on the ID3v2 tag; it would write anew every frame that does
        // not come back from the map as it went in. Pictures go to the ID3v2 tag alone.
        TagLib::ID3v2::Tag *tag = id3v2TagFor(file, true);
        if (!tag)
            return false;
        saveId3Properties(file, tag, was, map);
        if (picturesChanged && !writePictures(tag, after.pictures))
            return false;
    } else if (auto *mp4 = dynamic_cast<TagLib::MP4::File *>(file)) {
        // W-ONLY, MPEG-4: no setProperties; only the items of the keys the change names.
        TagLib::MP4::Tag *tag = mp4->tag();
        if (!tag)
            return false;
        saveMp4Properties(tag, was, map);
        if (picturesChanged) {
            if (!writePictures(file, after.pictures))
                return false;
            // An empty list leaves no covr item; the library leaves an empty one.
            if (after.pictures.isEmpty())
                tag->removeItem(TagLib::String("covr"));
        }
    } else {
        // W-ONLY, APEv2 (WavPack): the item names as the file spelt them, to give back.
        const ApeNames apeNames = apeNamesOf(file);
        file->setProperties(map);
        if (picturesChanged && !writePictures(file, after.pictures))
            return false;
        restoreApeNames(file, apeNames);
    }
    if (id3 && ratingFrames) {
        // W-RATING, "the frames whole": they win over the Rating entry. An empty list makes no
        // tag where there is none.
        if (TagLib::ID3v2::Tag *tag = id3v2TagFor(file, !ratingFrames->isEmpty()))
            putRatingFrames(tag, *ratingFrames);
    } else if (id3 && ratingTouched) {
        const bool create = !after.first(QStringLiteral("Rating")).isEmpty();
        if (TagLib::ID3v2::Tag *tag = id3v2TagFor(file, create))
            rewritePopm(tag, after);
    }
    const bool saved = id3 ? saveId3(file, options, version) : file->save();
    // WS-WRITTEN: the tag library says saved; the stream says whether every byte went down.
    return saved && !stream.failed();
}

// W-ID3VER, not kept: the new version of the file (read once) against what the save meant
// it to hold. One line per catalogue field, then per other tag, that differs; sorted.
QStringList whatIsNotKept(const QString &copyPath, const TrackTags &after)
{
    ReadOptions readOptions;
    readOptions.pictures = false;
    readOptions.pictureData = false;
    const ReadResult read = readTags(copyPath, readOptions);
    if (read.status != ReadResult::Status::Ok)
        return { };

    QStringList lines;
    auto compare = [&lines](const QString &name, const QStringList &meant, const QStringList &is) {
        // A value nobody asked for is not a value that was lost.
        if (meant.isEmpty() || meant == is)
            return;
        if (is.isEmpty())
            lines.append(name);
        else
            lines.append(name + QStringLiteral(": ") + meant.join(QStringLiteral("; "))
                + QStringLiteral(" became ") + is.join(QStringLiteral("; ")));
    };
    for (const QString &name : tagFieldNames())
        compare(name, after.fields.value(name), read.tags.fields.value(name));
    // Four keys with no ID3v2.3 frame are named as a person would write them.
    auto readable = [](const QString &key) {
        if (key == QStringLiteral("DISCSUBTITLE"))
            return QStringLiteral("Disc Subtitle");
        if (key == QStringLiteral("RELEASEDATE"))
            return QStringLiteral("Release Date");
        if (key == QStringLiteral("TAGGINGDATE"))
            return QStringLiteral("Tagging Date");
        if (key == QStringLiteral("PRODUCEDNOTICE"))
            return QStringLiteral("Produced Notice");
        return key;
    };
    for (auto it = after.other.cbegin(); it != after.other.cend(); ++it)
        compare(readable(it.key()), it.value(), read.tags.other.value(it.key()));
    lines.sort();
    return lines;
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

// WS-SAME: the file is held with flock(2), exclusive, for the whole of a save. Closing the
// descriptor lets go, so every way out of the save releases it.
class FileHold {
public:
    FileHold() = default;
    FileHold(const FileHold &) = delete;
    FileHold &operator=(const FileHold &) = delete;
    ~FileHold()
    {
        if (m_fd >= 0)
            ::close(m_fd);
    }

    // False only when another holds the file. A file that cannot be opened for it, or a file
    // system with no such locks, is not a refusal: the save goes on without.
    bool take(const QString &path)
    {
        const int fd = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_CLOEXEC);
        if (fd < 0)
            return true;
        if (::flock(fd, LOCK_EX | LOCK_NB) != 0 && errno == EWOULDBLOCK) {
            ::close(fd);
            return false;
        }
        m_fd = fd;
        return true;
    }

private:
    int m_fd = -1;
};

// WS-SAME: what the system says of a file: which file it is, its size, when its contents and
// when its record were last changed.
bool sameFile(const struct stat &a, const struct stat &b)
{
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino && a.st_size == b.st_size
        && a.st_mtim.tv_sec == b.st_mtim.tv_sec && a.st_mtim.tv_nsec == b.st_mtim.tv_nsec
        && a.st_ctim.tv_sec == b.st_ctim.tv_sec && a.st_ctim.tv_nsec == b.st_ctim.tv_nsec;
}

WriteResult refusal(WriteResult::Status status, const QString &line)
{
    WriteResult result;
    result.status = status;
    result.problems = { line };
    return result;
}

}

std::optional<QList<RatingFrame>> readRatingFrames(const QString &filePath)
{
    try {
        const QByteArray name = QFile::encodeName(filePath);
        TagLib::FileRef ref(name.constData(), false);
        if (ref.isNull() || !ref.file() || !ref.file()->isValid())
            return std::nullopt;
        const TagLib::ID3v2::Tag *tag = nullptr;
        if (auto *mpeg = dynamic_cast<TagLib::MPEG::File *>(ref.file())) {
            if (mpeg->hasID3v2Tag())
                tag = mpeg->ID3v2Tag();
        } else if (auto *wav = dynamic_cast<TagLib::RIFF::WAV::File *>(ref.file())) {
            if (wav->hasID3v2Tag())
                tag = wav->ID3v2Tag();
        } else {
            return std::nullopt; // not of the ID3v2 family
        }
        return ratingFramesOf(tag);
    } catch (...) {
        return std::nullopt;
    }
}

WriteResult writeTags(
    const QString &filePath, const TagChanges &changes, const WriteOptions &options)
{
    // WS-SAME: the first look at the file, before it is read. stat follows a symbolic link,
    // so it is the file the path leads to.
    struct stat firstLook { };
    const bool looked = ::stat(QFile::encodeName(filePath).constData(), &firstLook) == 0;
    if (options.afterFirstLook)
        options.afterFirstLook();

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
    // W-UNCHANGED, "the frames whole": the same stars from other frames is a change.
    // A file with no ID3v2 family has no frames to differ: the list means nothing for it.
    bool framesAsAre = true;
    if (changes.ratingFrames) {
        const std::optional<QList<RatingFrame>> have = readRatingFrames(filePath);
        framesAsAre = !have || *have == *changes.ratingFrames;
    }
    if (tags::sameTags(read.tags, after) && framesAsAre) {
        result.status = WriteResult::Status::Unchanged;
        return result;
    }

    // WS-LINK: a file with more than one name is not saved; a save would replace one name
    // and leave the others with the old version. A file that could not be looked at is not
    // saved either.
    if (!looked) {
        result.status = WriteResult::Status::Failed;
        return result;
    }
    if (firstLook.st_nlink > 1) {
        return refusal(WriteResult::Status::HardLinked,
            QStringLiteral("the file has %1 names (hard links), and linuxBee does not change the "
                           "tags of such a file; to tag it here, make it a file of its own "
                           "first: cp -p FILE FILE.new && mv FILE.new FILE")
                .arg(qlonglong(firstLook.st_nlink)));
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

    // WS-SAME: for the whole of the save the file is held; a save that finds it held does not
    // wait. The hold ends with this function, however it ends.
    FileHold hold;
    if (!hold.take(target)) {
        return refusal(WriteResult::Status::ChangedOnDisk,
            QStringLiteral("another save of this file is under way; nothing was written, try "
                           "again"));
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
    bool wroteV23 = false;
    try {
        saved = saveChanges(copyPath, read.tags, checked, after, options,
            changes.pictures.has_value(), changes.ratingFrames, wroteV23);
    } catch (...) {
        // writeTags never throws; a tag library failure is Failed.
        saved = false;
    }
    if (!saved) {
        QFile::remove(copyPath);
        result.status = WriteResult::Status::Failed;
        return result;
    }

    // W-ID3VER, not kept: only an ID3v2.3 tag is read again, once, before the copy replaces
    // the original. The lines go into the result only when the save ends Ok.
    QStringList notKept;
    if (wroteV23)
        notKept = whatIsNotKept(copyPath, after);

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

    // WS-SAME: the second look, immediately before the swap and after the hook. The file that
    // is replaced is the file that was read: if it is gone, or anything the system says of it
    // differs from the first look, the copy goes and the other program's change stays.
    struct stat secondLook { };
    if (::stat(QFile::encodeName(target).constData(), &secondLook) != 0
        || !sameFile(firstLook, secondLook)) {
        QFile::remove(copyPath);
        return refusal(WriteResult::Status::ChangedOnDisk,
            QStringLiteral("the file was changed by another program while it was being saved; "
                           "nothing was written, try again"));
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
    result.notKept = notKept;
    return result;
}

}
