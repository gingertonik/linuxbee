#include "tagreader.h"

#include <QFile>
#include <QFileInfo>

#include <fileref.h>
#include <flacfile.h>
#include <flacproperties.h>
#include <id3v2header.h>
#include <id3v2tag.h>
#include <mp4file.h>
#include <mp4properties.h>
#include <mpegfile.h>
#include <opusfile.h>
#include <tdebuglistener.h>
#include <vorbisfile.h>
#include <wavfile.h>
#include <wavpackfile.h>
#include <wavpackproperties.h>
#include <wavproperties.h>

// Stub. Plan 03 replaces every body in this file.
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

}

QString TrackTags::first(const QString &fieldName) const
{
    Q_UNUSED(fieldName);
    return { };
}

QString TrackTags::joined(const QString &fieldName) const
{
    Q_UNUSED(fieldName);
    return { };
}

ReadResult readTags(const QString &filePath, const ReadOptions &options)
{
    Q_UNUSED(options);
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
        tags.audio.durationMs = properties->lengthInMilliseconds();
        tags.audio.bitrateKbps = properties->bitrate();
        tags.audio.sampleRateHz = properties->sampleRate();
        tags.audio.channels = properties->channels();
        tags.audio.bitsPerSample = bitsPerSampleOf(properties);
        return result;
    } catch (...) {
        // readTags never throws; a file TagLib cannot handle is not audio here.
        return ReadResult { ReadResult::Status::NotAudio, { } };
    }
}

QString imageMimeType(const QByteArray &data)
{
    Q_UNUSED(data);
    return { };
}

TrackTagSource::TrackTagSource(const TrackTags &tags)
    : m_tags(tags)
{
}

std::optional<QString> TrackTagSource::value(const QString &fieldName) const
{
    Q_UNUSED(fieldName);
    Q_UNUSED(m_tags);
    return std::nullopt;
}

}
