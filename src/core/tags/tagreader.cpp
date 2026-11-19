#include "tagreader.h"

#include "catalogue.h"

#include <QFile>
#include <QFileInfo>

#include <algorithm>

#include <fileref.h>
#include <flacfile.h>
#include <flacproperties.h>
#include <id3v2header.h>
#include <id3v2tag.h>
#include <mp4file.h>
#include <mp4properties.h>
#include <popularimeterframe.h>
#include <mpegfile.h>
#include <oggfile.h>
#include <oggpageheader.h>
#include <opusfile.h>
#include <tdebuglistener.h>
#include <tpropertymap.h>
#include <vorbisfile.h>
#include <wavfile.h>
#include <wavpackfile.h>
#include <wavpackproperties.h>
#include <wavproperties.h>

namespace linuxbee {

namespace {

// R-QUIET: TagLib's own diagnostics are dropped. The listener is process-wide,
// so it is installed once; a function-local static is initialised thread-safely.
class QuietListener : public TagLib::DebugListener {
public:
    void printMessage(const TagLib::String &) override { }
};

void silenceTagLib()
{
    static QuietListener listener;
    static const bool installed = [] {
        TagLib::setDebugListener(&listener);
        return true;
    }();
    Q_UNUSED(installed);
}

// R-FORMAT: the container TagLib found; the extension only when TagLib has no name for it.
QString formatName(TagLib::File *file, const QString &path)
{
    if (dynamic_cast<TagLib::MPEG::File *>(file))
        return QStringLiteral("mp3");
    if (dynamic_cast<TagLib::FLAC::File *>(file))
        return QStringLiteral("flac");
    if (dynamic_cast<TagLib::Ogg::Vorbis::File *>(file))
        return QStringLiteral("ogg");
    if (dynamic_cast<TagLib::Ogg::Opus::File *>(file))
        return QStringLiteral("opus");
    if (dynamic_cast<TagLib::MP4::File *>(file))
        return QStringLiteral("m4a");
    if (dynamic_cast<TagLib::RIFF::WAV::File *>(file))
        return QStringLiteral("wav");
    if (dynamic_cast<TagLib::WavPack::File *>(file))
        return QStringLiteral("wv");
    return QFileInfo(path).suffix().toLower();
}

// R-TAGTYPES: the order is ID3v2, APEv2, ID3v1, Vorbis, MP4, RIFF Info.
QString id3v2Name(const TagLib::ID3v2::Tag *tag)
{
    return QStringLiteral("ID3v2.%1").arg(tag->header()->majorVersion());
}

QStringList tagTypesOf(TagLib::File *file)
{
    QStringList types;
    if (auto *mpeg = dynamic_cast<TagLib::MPEG::File *>(file)) {
        if (mpeg->hasID3v2Tag())
            types << id3v2Name(mpeg->ID3v2Tag());
        if (mpeg->hasAPETag())
            types << QStringLiteral("APEv2");
        if (mpeg->hasID3v1Tag())
            types << QStringLiteral("ID3v1");
    } else if (auto *flac = dynamic_cast<TagLib::FLAC::File *>(file)) {
        if (flac->hasID3v2Tag())
            types << id3v2Name(flac->ID3v2Tag());
        if (flac->hasID3v1Tag())
            types << QStringLiteral("ID3v1");
        if (flac->hasXiphComment())
            types << QStringLiteral("Vorbis");
    } else if (dynamic_cast<TagLib::Ogg::Vorbis::File *>(file)
        || dynamic_cast<TagLib::Ogg::Opus::File *>(file)) {
        types << QStringLiteral("Vorbis");
    } else if (auto *mp4 = dynamic_cast<TagLib::MP4::File *>(file)) {
        if (mp4->hasMP4Tag())
            types << QStringLiteral("MP4");
    } else if (auto *wav = dynamic_cast<TagLib::RIFF::WAV::File *>(file)) {
        if (wav->hasID3v2Tag())
            types << id3v2Name(wav->ID3v2Tag());
        if (wav->hasInfoTag())
            types << QStringLiteral("RIFF Info");
    } else if (auto *wavpack = dynamic_cast<TagLib::WavPack::File *>(file)) {
        if (wavpack->hasAPETag())
            types << QStringLiteral("APEv2");
        if (wavpack->hasID3v1Tag())
            types << QStringLiteral("ID3v1");
    }
    return types;
}

// R-RATING, ID3v2: the rating byte of the first POPM frame. Returns true when
// the file has a POPM frame; `stars` is set when that byte is a rating.
bool popmRatingOf(TagLib::File *file, std::optional<double> &stars)
{
    const TagLib::ID3v2::Tag *tag = nullptr;
    if (auto *mpeg = dynamic_cast<TagLib::MPEG::File *>(file)) {
        if (mpeg->hasID3v2Tag())
            tag = mpeg->ID3v2Tag();
    } else if (auto *wav = dynamic_cast<TagLib::RIFF::WAV::File *>(file)) {
        if (wav->hasID3v2Tag())
            tag = wav->ID3v2Tag();
    }
    if (!tag)
        return false;

    const TagLib::ID3v2::FrameList frames = tag->frameList(TagLib::ByteVector("POPM"));
    if (frames.isEmpty())
        return false;
    if (auto *popm = dynamic_cast<const TagLib::ID3v2::PopularimeterFrame *>(frames.front()))
        stars = ratingFromPopm(popm->rating());
    return true;
}

// TagLib's Variant text, as a Qt string.
QString variantText(const TagLib::VariantMap &map, const char *key)
{
    return QString::fromUtf8(map.value(key).toString().toCString(true));
}

// R-PICTURES: TagLib's PICTURE complex property, in file order.
QList<Picture> picturesOf(TagLib::File *file, const ReadOptions &options)
{
    QList<Picture> pictures;
    if (!options.pictures)
        return pictures;

    for (const TagLib::VariantMap &map : file->complexProperties(TagLib::String("PICTURE"))) {
        const TagLib::ByteVector bytes = map.value("data").toByteVector();
        Picture picture;
        picture.byteCount = static_cast<qsizetype>(bytes.size());
        if (options.pictureData)
            picture.data = QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size()));

        // R-PICTURES: a picture with no type is a front cover.
        picture.type = variantText(map, "pictureType");
        if (picture.type.isEmpty())
            picture.type = QStringLiteral("Front Cover");
        picture.description = variantText(map, "description");

        // R-MIME: a tag with no MIME type (an APEv2 item) is sniffed from its bytes.
        picture.mimeType = variantText(map, "mimeType");
        if (picture.mimeType.isEmpty()) {
            const QByteArray head(
                bytes.data(), static_cast<qsizetype>(qMin<size_t>(bytes.size(), 16)));
            picture.mimeType = imageMimeType(head);
        }
        pictures.append(picture);
    }
    return pictures;
}

// TagLib's property keys and values, as UTF-8 Qt strings.
QMap<QString, QStringList> toQtProperties(const TagLib::PropertyMap &propertyMap)
{
    QMap<QString, QStringList> out;
    for (auto it = propertyMap.begin(); it != propertyMap.end(); ++it) {
        QStringList values;
        for (const TagLib::String &value : it->second)
            values.append(QString::fromUtf8(value.toCString(true)));
        out.insert(QString::fromUtf8(it->first.toCString(true)), values);
    }
    return out;
}

// R-UNSUPPORTED: pictures, POPM and the ID3v2.3 date frames are reported elsewhere.
// A prefix such as UNKNOWN/ is dropped before the frame name is compared.
QStringList unsupportedNames(const TagLib::PropertyMap &propertyMap)
{
    static const QStringList skipped = {
        QStringLiteral("APIC"),
        QStringLiteral("covr"),
        QStringLiteral("POPM"),
        QStringLiteral("METADATA_BLOCK_PICTURE"),
        QStringLiteral("TYER"),
        QStringLiteral("TDAT"),
        QStringLiteral("TIME"),
    };
    QStringList names;
    for (const TagLib::String &name : propertyMap.unsupportedData()) {
        const QString text = QString::fromUtf8(name.toCString(true));
        const QString frame = text.mid(text.lastIndexOf(QLatin1Char('/')) + 1);
        // An APEv2 cover art item, such as COVER ART (FRONT), is a picture (R-PICTURES).
        if (skipped.contains(frame)
            || frame.startsWith(QLatin1String("COVER ART"), Qt::CaseInsensitive))
            continue;
        names.append(text);
    }
    return names;
}

// R-AUDIO: only FLAC, WAV, WavPack and MPEG-4 report a bit depth; the rest are 0.
int bitsPerSampleOf(const TagLib::AudioProperties *properties)
{
    if (auto *flac = dynamic_cast<const TagLib::FLAC::Properties *>(properties))
        return flac->bitsPerSample();
    if (auto *wav = dynamic_cast<const TagLib::RIFF::WAV::Properties *>(properties))
        return wav->bitsPerSample();
    if (auto *wavpack = dynamic_cast<const TagLib::WavPack::Properties *>(properties))
        return wavpack->bitsPerSample();
    if (auto *mp4 = dynamic_cast<const TagLib::MP4::Properties *>(properties))
        return mp4->bitsPerSample();
    return 0;
}

// R-AUDIO: the stream position of the last Ogg page that ends where the file ends, or -1.
// TagLib looks for the last page in reads that do not overlap and can miss it.
qint64 lastOggPagePosition(const QString &filePath)
{
    constexpr qint64 kMaxPage = 27 + 255 + 255 * 255;
    QFile in(filePath);
    if (!in.open(QIODevice::ReadOnly))
        return -1;
    const qint64 size = in.size();
    const qint64 start = std::max<qint64>(0, size - kMaxPage);
    if (!in.seek(start))
        return -1;
    const QByteArray tail = in.read(size - start);
    if (tail.size() != size - start)
        return -1;
    const auto *bytes = reinterpret_cast<const unsigned char *>(tail.constData());
    for (qsizetype at = tail.lastIndexOf("OggS"); at >= 0;
        at = at > 0 ? tail.lastIndexOf("OggS", at - 1) : -1) {
        if (at + 27 > tail.size() || bytes[at + 4] != 0)
            continue;
        const qsizetype segments = bytes[at + 26];
        if (at + 27 + segments > tail.size())
            continue;
        qsizetype length = 27 + segments;
        for (qsizetype i = 0; i < segments; ++i)
            length += bytes[at + 27 + i];
        if (at + length != tail.size())
            continue;
        quint64 position = 0;
        for (int i = 7; i >= 0; --i)
            position = (position << 8) | bytes[at + 6 + i];
        return static_cast<qint64>(position);
    }
    return -1;
}

// R-AUDIO: what to add to TagLib's length when it took a page before the last one.
qint64 missedOggMilliseconds(TagLib::File *file, const QString &filePath, int sampleRate)
{
    auto *ogg = dynamic_cast<TagLib::Ogg::File *>(file);
    if (!ogg || sampleRate <= 0)
        return 0;
    const TagLib::Ogg::PageHeader *taken = ogg->lastPageHeader();
    if (!taken)
        return 0;
    const qint64 last = lastOggPagePosition(filePath);
    const qint64 was = taken->absoluteGranularPosition();
    if (last < 0 || last <= was)
        return 0;
    return (last - was) * 1000 / sampleRate;
}

}

ReadResult readTags(const QString &filePath, const ReadOptions &options)
{
    silenceTagLib();

    // R-STATUS: a path that is not a readable regular file is NotFound.
    ReadResult result;
    const QFileInfo info(filePath);
    if (filePath.isEmpty() || !info.isFile() || !info.isReadable())
        return result;

    try {
        // R-STATUS: no audio stream with a sample rate and channels is NotAudio.
        result.status = ReadResult::Status::NotAudio;
        const QByteArray name = QFile::encodeName(filePath);
        TagLib::FileRef ref(name.constData(), true, TagLib::AudioProperties::Accurate);
        if (ref.isNull())
            return result;
        TagLib::File *file = ref.file();
        const TagLib::AudioProperties *properties = ref.audioProperties();
        if (!file->isValid() || !properties || properties->sampleRate() <= 0
            || properties->channels() <= 0)
            return result;

        result.status = ReadResult::Status::Ok;
        TrackTags &tags = result.tags;
        tags.format = formatName(file, filePath);
        tags.tagTypes = tagTypesOf(file);
        tags.audio.durationMs = properties->lengthInMilliseconds()
            + missedOggMilliseconds(file, filePath, properties->sampleRate());
        tags.audio.bitrateKbps = properties->bitrate();
        tags.audio.sampleRateHz = properties->sampleRate();
        tags.audio.channels = properties->channels();
        tags.audio.bitsPerSample = bitsPerSampleOf(properties);

        const TagLib::PropertyMap propertyMap = file->properties();
        tags.unsupported = unsupportedNames(propertyMap);
        const bool id3 = tags.format == QLatin1String("mp3") || tags.format == QLatin1String("wav");

        // R-RATING: POPM wins over a RATING text. The losing text is kept in other.
        QMap<QString, QStringList> fieldProperties = toQtProperties(propertyMap);
        std::optional<double> popmStars;
        const bool hasPopm = popmRatingOf(file, popmStars);
        const QStringList ratingText
            = hasPopm ? fieldProperties.take(QStringLiteral("RATING")) : QStringList { };
        tags::assignFields(fieldProperties, id3, tags);
        tags.pictures = picturesOf(file, options);
        if (hasPopm) {
            tags.fields.remove(QStringLiteral("Rating"));
            if (popmStars)
                tags.fields.insert(QStringLiteral("Rating"), { ratingToString(*popmStars) });
            QStringList losing;
            for (const QString &value : ratingText) {
                if (!value.trimmed().isEmpty())
                    losing.append(value);
            }
            if (!losing.isEmpty())
                tags.other.insert(QStringLiteral("RATING"), losing);
        }
        return result;
    } catch (...) {
        // readTags never throws; a file TagLib cannot handle is not audio here.
        return ReadResult { ReadResult::Status::NotAudio, { } };
    }
}

QString imageMimeType(const QByteArray &data)
{
    // R-MIME: the first bytes of each format.
    if (data.startsWith(QByteArray::fromHex("89504e470d0a1a0a")))
        return QStringLiteral("image/png");
    if (data.startsWith(QByteArray::fromHex("ffd8ff")))
        return QStringLiteral("image/jpeg");
    if (data.startsWith("GIF87a") || data.startsWith("GIF89a"))
        return QStringLiteral("image/gif");
    if (data.startsWith("BM"))
        return QStringLiteral("image/bmp");
    if (data.size() >= 12 && data.startsWith("RIFF") && data.mid(8, 4) == "WEBP")
        return QStringLiteral("image/webp");
    return { };
}

}
