#pragma once
#include "converter.h"

namespace linuxbee {

// One row of the format table (C-FORMATS). Private to src/core/convert:
// formats.cpp defines the table and settings.cpp reads the same rows, so
// the ids, extensions and muxers live in one place.
struct FormatInfo {
    AudioFormat format;
    const char *id; // "mp3"
    const char *extension; // without a dot: "m4a"
    const char *muxer; // the ffmpeg -f name: "ipod"
    bool lossless;
};

const FormatInfo &formatInfo(AudioFormat format);

}
