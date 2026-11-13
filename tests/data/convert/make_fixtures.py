#!/usr/bin/env python3
"""Builds the converter fixtures in this directory.

Run from the repository root:   python3 tests/data/convert/make_fixtures.py

Needs ffmpeg and the Python package mutagen. The fixtures are committed; this
script is the record of what each file holds. Every tag value a test expects
is written here by mutagen, not by the program under test.

tone.flac    1 s, 440 Hz sine, stereo, 16-bit, 44100 Hz, tags and a picture
hires.flac   0.5 s, 1000 Hz sine, stereo, 24-bit, 96000 Hz, a title only
tone.mp3     1 s, 440 Hz sine, stereo, 128 kbps, ID3v2.3 tags
"""
import os
import struct
import subprocess
import sys
import zlib

from mutagen.flac import FLAC, Picture
from mutagen.id3 import ID3, POPM, TALB, TIT2, TPE1, TRCK, TXXX

HERE = os.path.dirname(os.path.abspath(__file__))


def png_1x1() -> bytes:
    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    ihdr = struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0)
    idat = zlib.compress(b"\x00\xff\x00\x00")
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", idat) + chunk(b"IEND", b"")


def path(name: str) -> str:
    return os.path.join(HERE, name)


def tone(name: str, frequency: int, rate: int, seconds: str, *codec: str) -> str:
    out = path(name)
    subprocess.run(
        ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
         f"sine=frequency={frequency}:sample_rate={rate}:duration={seconds}", "-ac", "2",
         "-map_metadata", "-1", "-fflags", "+bitexact", "-flags:a", "+bitexact", *codec, out],
        check=True)
    return out


def main() -> int:
    f = tone("tone.flac", 440, 44100, "1", "-c:a", "flac", "-sample_fmt", "s16")
    flac = FLAC(f)
    flac.delete()
    flac["TITLE"] = ["Tone Title"]
    flac["ARTIST"] = ["Tone Artist", "Second Artist"]
    flac["ALBUMARTIST"] = ["Tone Artist"]
    flac["ALBUM"] = ["Tone Album"]
    flac["DATE"] = ["2001-02-03"]
    flac["TRACKNUMBER"] = ["3"]
    flac["TRACKTOTAL"] = ["12"]
    flac["DISCNUMBER"] = ["1"]
    flac["GENRE"] = ["Test"]
    flac["COMMENT"] = ["A \u00fcnicode comment \u65e5\u672c"]
    flac["RATING"] = ["80"]
    flac["MY KEY"] = ["my value"]
    flac["ENCODER"] = ["some encoder 1.0"]
    pic = Picture()
    pic.type = 3
    pic.mime = "image/png"
    pic.desc = "front"
    pic.width = pic.height = 1
    pic.depth = 24
    pic.data = png_1x1()
    flac.add_picture(pic)
    flac.save()

    f = tone("hires.flac", 1000, 96000, "0.5", "-c:a", "flac", "-sample_fmt", "s32",
             "-bits_per_raw_sample", "24")
    flac = FLAC(f)
    flac.delete()
    flac["TITLE"] = ["Hi Res"]
    flac.save()

    f = tone("tone.mp3", 440, 44100, "1", "-c:a", "libmp3lame", "-b:a", "128k",
             "-id3v2_version", "0", "-write_id3v1", "0")
    tags = ID3()
    tags.add(TIT2(encoding=1, text=["Lossy Title"]))
    tags.add(TPE1(encoding=1, text=["Lossy Artist"]))
    tags.add(TALB(encoding=1, text=["Lossy Album"]))
    tags.add(TRCK(encoding=1, text=["2/5"]))
    tags.add(POPM(email="", rating=255, count=0))
    # A tag whose name the tag writer refuses (not ASCII): it cannot be carried over.
    tags.add(TXXX(encoding=1, desc="CL\u00c9", text=["v"]))
    tags.save(f, v2_version=3, v1=0)

    lines = []
    for name in ("tone.flac", "hires.flac", "tone.mp3"):
        probe = subprocess.run(
            ["ffprobe", "-v", "error", "-show_entries",
             "stream=codec_name,sample_rate,channels,bits_per_raw_sample,duration", "-of",
             "csv=p=0", path(name)], capture_output=True, text=True)
        md5 = subprocess.run(
            ["ffmpeg", "-v", "error", "-i", path(name), "-map", "0:a", "-c:a", "pcm_s32le", "-f",
             "md5", "-"], capture_output=True, text=True)
        lines.append(f"== {name} ({os.path.getsize(path(name))} bytes)")
        lines.append("ffprobe: " + " | ".join(probe.stdout.split()))
        lines.append("decoded as 32-bit PCM: " + md5.stdout.strip())
    with open(path("MANIFEST.txt"), "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
