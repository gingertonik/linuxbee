#pragma once
#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <optional>

#include "template/template.h"

// Reads the tags, pictures and audio properties of one music file.
// Contract: docs/spec/tag-reading.md. Nothing here writes to the disk.
// No TagLib type appears in this header.
namespace linuxbee {

struct AudioInfo {
    int durationMs = 0;
    int bitrateKbps = 0;
    int sampleRateHz = 0;
    int channels = 0;
    int bitsPerSample = 0; // 0 when the format has no such notion (MP3, Vorbis, ...)
};

struct Picture {
    QString type; // the ID3v2 picture type name, e.g. "Front Cover"
    QString mimeType;
    QString description;
    QByteArray data; // empty when ReadOptions::pictureData is false
    qsizetype byteCount = 0; // size of the picture, also when data was not kept
};

struct TrackTags {
    // R-FORMAT: "mp3", "flac", "ogg", "opus", "m4a", "wav", "wv", ...
    QString format;
    // R-TAGTYPES: the kinds of tag found, e.g. { "ID3v2.4" } or { "ID3v2.3", "ID3v1" }.
    QStringList tagTypes;
    AudioInfo audio;

    // R-FIELDS: values by linuxBee field name ("Title", "Album Artist",
    // "Track#"). A field with no value has no entry. No value is ever empty.
    QMap<QString, QStringList> fields;
    // R-OTHER: every other text tag, by the key the tag library reports.
    QMap<QString, QStringList> other;
    // R-UNSUPPORTED: tags that are present but not text (names only).
    QStringList unsupported;
    QList<Picture> pictures;

    // First value of the field, or an empty string.
    QString first(const QString &fieldName) const;
    // All values joined with "; ", or an empty string.
    QString joined(const QString &fieldName) const;
};

struct ReadOptions {
    bool pictures = true; // false: TrackTags::pictures stays empty
    bool pictureData = true; // false: Picture::data stays empty, byteCount is still set
};

struct ReadResult {
    enum class Status { Ok, NotFound, NotAudio };
    Status status = Status::NotFound;
    TrackTags tags; // meaningful for Status::Ok only
};

ReadResult readTags(const QString &filePath, const ReadOptions &options = { });

// R-CATALOGUE: the field names readTags can produce, in display order.
QStringList tagFieldNames();

// R-RATING. Stars from 0.5 to 5 in steps of 0.5; nullopt means "no rating".
std::optional<double> ratingFromPopm(int popmValue);
std::optional<double> ratingFromText(const QString &text);
// "4", "3.5": no trailing zero.
QString ratingToString(double stars);

// R-MIME: "image/png", "image/jpeg", "image/gif", "image/bmp", "image/webp",
// or an empty string when the bytes are none of these.
QString imageMimeType(const QByteArray &data);

// R-SOURCE: lets a naming template read a TrackTags. A catalogue field with
// no value is known and blank; any other name is unknown.
class TrackTagSource : public TagSource {
public:
    explicit TrackTagSource(const TrackTags &tags);
    std::optional<QString> value(const QString &fieldName) const override;

private:
    const TrackTags &m_tags;
};

}
