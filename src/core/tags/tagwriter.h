#pragma once
#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <optional>

#include "tags/tagreader.h"

// Changes the tags and pictures of one music file, in place.
// Contract: docs/spec/tag-writing.md. No TagLib type appears in this header.
namespace linuxbee {

struct TagChanges {
    // Catalogue field name -> its new values. An empty list removes the
    // field. A field that is not named here is left exactly as it is.
    QMap<QString, QStringList> fields;
    // The same for tags outside the catalogue, by key (TrackTags::other).
    QMap<QString, QStringList> other;
    // When set, the file's embedded pictures become exactly this list.
    std::optional<QList<Picture>> pictures;

    bool isEmpty() const { return fields.isEmpty() && other.isEmpty() && !pictures.has_value(); }
};

struct WriteOptions {
    enum class Id3Version { V23, V24 };
    // W-ID3VER: used when an ID3v2 tag is written (MP3, WAV).
    Id3Version id3Version = Id3Version::V23;
    // W-ID3V1: MP3 only. true keeps an ID3v1 tag in step; false removes it.
    bool writeId3v1 = true;
    // W-MTIME: true puts the file's modification time back after saving.
    bool keepModificationTime = false;
};

struct WriteResult {
    enum class Status {
        Ok, // saved
        Unchanged, // the file already says this; nothing was written
        NotFound,
        NotAudio,
        NotWritable,
        InvalidChange, // nothing was written; see problems
        Failed, // the tag library could not save
    };
    Status status = Status::NotFound;
    // InvalidChange: one line per refused entry, each starting with the
    // field name or key it is about.
    QStringList problems;

    bool ok() const { return status == Status::Ok || status == Status::Unchanged; }
};

WriteResult writeTags(
    const QString &filePath, const TagChanges &changes, const WriteOptions &options = { });

// W-RATING: the POPM byte written for a star rating (0.5 to 5 in halves).
// Returns 0 for anything else.
int popmFromRating(double stars);

}
