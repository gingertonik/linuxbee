#!/usr/bin/env python3
"""Builds the tag-reading fixtures in this directory.

Run from the repository root:   python3 tests/data/tags/make_fixtures.py

Needs ffmpeg (with libmp3lame, libvorbis, libopus, flac, aac, wavpack) and the
Python package mutagen. The fixtures are committed, so nobody has to run this
to build or test; it is kept as the record of exactly what each file holds.

Every value a test expects is written here, by a tagging library that is not
the one the program uses. MANIFEST.txt, written at the end, is a read-back of
each file by mutagen and ffprobe, as a second opinion.

Audio: 0.25 s of silence, 44100 Hz, stereo (WAV: 16-bit).
"""
import base64
import json
import os
import struct
import subprocess
import sys
import zlib

from mutagen.apev2 import BINARY, APEv2, APEValue
from mutagen.flac import FLAC, Picture
from mutagen.id3 import (APIC, COMM, ID3, POPM, TALB, TBPM, TCOM, TCON, TDRC, TIT1, TIT2, TPE1,
                         TPE2, TPOS, TRCK, USLT, TXXX)
from mutagen.mp4 import MP4, MP4Cover
from mutagen.oggopus import OggOpus
from mutagen.oggvorbis import OggVorbis
from mutagen.wave import WAVE

HERE = os.path.dirname(os.path.abspath(__file__))


def png_1x1() -> bytes:
    """A valid 1x1 red PNG, built here so no outside file is needed."""
    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    ihdr = struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0)
    idat = zlib.compress(b"\x00\xff\x00\x00")
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", idat) + chunk(b"IEND", b"")


PNG = png_1x1()


def path(name: str) -> str:
    return os.path.join(HERE, name)


def silence(name: str, *codec: str) -> str:
    out = path(name)
    subprocess.run(
        ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
         "anullsrc=r=44100:cl=stereo", "-t", "0.25", "-map_metadata", "-1",
         "-fflags", "+bitexact", "-flags:a", "+bitexact", *codec, out],
        check=True)
    return out


def flac_picture() -> Picture:
    pic = Picture()
    pic.type = 3  # front cover
    pic.mime = "image/png"
    pic.desc = "front"
    pic.width = pic.height = 1
    pic.depth = 24
    pic.data = PNG
    return pic


def main() -> int:
    # --- basic.mp3: ID3v2.4, UTF-8, no ID3v1 ---------------------------------
    f = silence("basic.mp3", "-c:a", "libmp3lame", "-b:a", "128k", "-id3v2_version", "0",
                "-write_id3v1", "0", "-write_xing", "0")
    tags = ID3()
    tags.add(TIT2(encoding=3, text=["Someone like You"]))
    tags.add(TPE1(encoding=3, text=["Adele"]))
    tags.add(TPE2(encoding=3, text=["Adele"]))
    tags.add(TALB(encoding=3, text=["21"]))
    tags.add(TDRC(encoding=3, text=["2011-01-24"]))
    tags.add(TRCK(encoding=3, text=["11/12"]))
    tags.add(TPOS(encoding=3, text=["1/2"]))
    tags.add(TCON(encoding=3, text=["Pop", "Soul"]))
    tags.add(TCOM(encoding=3, text=["Adele Adkins", "Dan Wilson"]))
    tags.add(TBPM(encoding=3, text=["135"]))
    tags.add(TIT1(encoding=3, text=["Ballads"]))
    tags.add(COMM(encoding=3, lang="eng", desc="", text=["A comment"]))
    tags.add(USLT(encoding=3, lang="eng", desc="", text="I heard\nthat you're settled down"))
    tags.add(TXXX(encoding=3, desc="MOOD", text=["Sad"]))
    tags.add(TXXX(encoding=3, desc="MY CUSTOM", text=["kept"]))
    tags.add(POPM(email="someone@example.invalid", rating=196, count=7))
    tags.add(APIC(encoding=3, mime="image/png", type=3, desc="front", data=PNG))
    tags.save(f, v2_version=4, v1=0)

    # --- v23.mp3: ID3v2.3, UTF-16, plus an ID3v1 tag ---------------------------
    f = silence("v23.mp3", "-c:a", "libmp3lame", "-b:a", "128k", "-id3v2_version", "0",
                "-write_id3v1", "0", "-write_xing", "0")
    tags = ID3()
    tags.add(TIT2(encoding=1, text=["\u00dcn\u00efc\u00f6d\u00e9 \u65e5\u672c\u8a9e"]))
    tags.add(TPE1(encoding=1, text=["Bj\u00f6rk"]))
    tags.add(TALB(encoding=1, text=["Homogenic"]))
    # Saved as ID3v2.3, this becomes TYER "1997" and TDAT "2209" (day, month).
    tags.add(TDRC(encoding=1, text=["1997-09-22"]))
    tags.add(TRCK(encoding=1, text=["3"]))
    tags.add(POPM(email="someone@example.invalid", rating=128, count=0))
    tags.save(f, v2_version=3, v1=2)

    # --- v1only.mp3: nothing but an ID3v1.1 tag --------------------------------
    f = silence("v1only.mp3", "-c:a", "libmp3lame", "-b:a", "128k", "-id3v2_version", "0",
                "-write_id3v1", "0", "-write_xing", "0")
    v1 = (b"TAG" + b"V1 Title".ljust(30, b"\0") + b"V1 Artist".ljust(30, b"\0")
          + b"V1 Album".ljust(30, b"\0") + b"1985" + b"V1 comment".ljust(28, b"\0")
          + b"\0" + bytes([7]) + bytes([17]))  # track 7, genre 17 = Rock
    assert len(v1) == 128
    with open(f, "ab") as handle:
        handle.write(v1)

    # --- basic.flac ------------------------------------------------------------
    f = silence("basic.flac", "-c:a", "flac", "-sample_fmt", "s16")
    flac = FLAC(f)
    flac.delete()
    flac["TITLE"] = ["Flac Title"]
    flac["ARTIST"] = ["First Artist", "Second Artist"]
    flac["ALBUM"] = ["Flac Album"]
    flac["ALBUMARTIST"] = ["Various Artists"]
    flac["DATE"] = ["2011"]
    flac["TRACKNUMBER"] = ["5"]
    flac["TRACKTOTAL"] = ["10"]
    flac["DISCNUMBER"] = ["2"]
    flac["DISCTOTAL"] = ["3"]
    flac["GENRE"] = ["Rock", "Blues"]
    flac["ORGANIZATION"] = ["A Label"]
    flac["CONTENTGROUP"] = ["A Group"]
    flac["COMPILATION"] = ["1"]
    flac["RATING"] = ["80"]
    flac["UNSYNCEDLYRICS"] = ["la la"]
    flac["REPLAYGAIN_TRACK_GAIN"] = ["-6.50 dB"]
    flac.add_picture(flac_picture())
    flac.save()

    # --- empty.flac: audio, no tags at all -------------------------------------
    f = silence("empty.flac", "-c:a", "flac", "-sample_fmt", "s16")
    flac = FLAC(f)
    flac.delete()
    flac.clear_pictures()
    flac.save()

    # --- basic.ogg: Vorbis, the other spelling of the totals --------------------
    f = silence("basic.ogg", "-c:a", "libvorbis", "-q:a", "2")
    ogg = OggVorbis(f)
    ogg.delete()
    ogg["TITLE"] = ["Ogg Title"]
    ogg["ARTIST"] = ["Ogg Artist"]
    ogg["TRACKNUMBER"] = ["4"]
    ogg["TOTALTRACKS"] = ["9"]
    ogg["DISCNUMBER"] = ["1"]
    ogg["TOTALDISCS"] = ["1"]
    ogg["METADATA_BLOCK_PICTURE"] = [base64.b64encode(flac_picture().write()).decode("ascii")]
    ogg.save()

    # --- basic.opus ------------------------------------------------------------
    f = silence("basic.opus", "-c:a", "libopus", "-b:a", "64k")
    opus = OggOpus(f)
    opus.delete()
    opus["TITLE"] = ["Opus Title"]
    opus["ARTIST"] = ["Opus Artist"]
    opus["ALBUM"] = ["Opus Album"]
    opus["DATE"] = ["2020-05-06"]
    opus["TRACKNUMBER"] = ["2/8"]
    opus.save()

    # --- basic.m4a: AAC in MP4 -------------------------------------------------
    f = silence("basic.m4a", "-c:a", "aac", "-b:a", "96k")
    mp4 = MP4(f)
    mp4.delete()
    mp4["\xa9nam"] = ["M4A Title"]
    mp4["\xa9ART"] = ["M4A Artist"]
    mp4["\xa9alb"] = ["M4A Album"]
    mp4["aART"] = ["M4A Album Artist"]
    mp4["\xa9day"] = ["2015"]
    mp4["\xa9gen"] = ["Electronic"]
    mp4["\xa9wrt"] = ["M4A Composer"]
    mp4["trkn"] = [(3, 9)]
    mp4["disk"] = [(1, 2)]
    mp4["cpil"] = True
    mp4["tmpo"] = [128]
    mp4["covr"] = [MP4Cover(PNG, imageformat=MP4Cover.FORMAT_PNG)]
    mp4.save()

    # --- basic.wav: PCM with an ID3 chunk --------------------------------------
    f = silence("basic.wav", "-c:a", "pcm_s16le")
    wav = WAVE(f)
    wav.add_tags()
    wav.tags.add(TIT2(encoding=3, text=["Wav Title"]))
    wav.tags.add(TPE1(encoding=3, text=["Wav Artist"]))
    wav.tags.add(TRCK(encoding=3, text=["1"]))
    wav.save()

    # --- basic.wv: WavPack with APEv2 ------------------------------------------
    f = silence("basic.wv", "-c:a", "wavpack", "-sample_fmt", "s16p")
    ape = APEv2()
    ape["Title"] = "Wv Title"
    ape["Artist"] = "Wv Artist"
    ape["Album"] = "Wv Album"
    ape["Year"] = "2001"
    ape["Track"] = "6/7"
    ape["Disc"] = "1"
    # An APE picture item: a file name, a zero byte, then the image.
    ape["Cover Art (Front)"] = APEValue(b"cover.png\x00" + PNG, BINARY)
    ape.save(f)

    # --- files that are not what they claim ------------------------------------
    with open(path("notaudio.mp3"), "w", encoding="utf-8") as handle:
        handle.write("This is a text file with a music file's extension.\n")
    with open(path("basic.flac"), "rb") as src, open(path("truncated.flac"), "wb") as dst:
        dst.write(src.read(60))
    with open(path("basic.flac"), "rb") as src, open(path("noextension"), "wb") as dst:
        dst.write(src.read())
    open(path("zero.mp3"), "wb").close()

    # --- MANIFEST.txt: a read-back by mutagen and ffprobe ----------------------
    import mutagen
    lines = []
    for name in sorted(os.listdir(HERE)):
        if name in ("make_fixtures.py", "MANIFEST.txt"):
            continue
        full = path(name)
        lines.append(f"== {name}  ({os.path.getsize(full)} bytes)")
        try:
            m = mutagen.File(full)
        except Exception as error:  # noqa: BLE001 - a broken fixture is the point
            m = None
            lines.append(f"mutagen: error {type(error).__name__}")
        if m is None:
            lines.append("mutagen: not recognised")
        else:
            info = m.info
            lines.append(f"mutagen: length={getattr(info, 'length', 0):.3f}s "
                         f"sample_rate={getattr(info, 'sample_rate', 0)} "
                         f"channels={getattr(info, 'channels', 0)} "
                         f"bits_per_sample={getattr(info, 'bits_per_sample', 0)}")
            for key in sorted(m.tags.keys() if m.tags else []):
                value = m.tags[key]
                text = repr(value)
                if len(text) > 100:
                    text = text[:100] + "..."
                lines.append(f"  {key} = {text}")
        probe = subprocess.run(
            ["ffprobe", "-v", "error", "-show_entries",
             "format=format_name,duration:stream=codec_name,sample_rate,channels",
             "-of", "json", full], capture_output=True, text=True)
        try:
            data = json.loads(probe.stdout)
            fmt = data.get("format", {})
            streams = [s.get("codec_name") for s in data.get("streams", [])]
            lines.append(f"ffprobe: format={fmt.get('format_name')} duration={fmt.get('duration')} "
                         f"streams={streams}")
        except json.JSONDecodeError:
            lines.append("ffprobe: no output")
    with open(path("MANIFEST.txt"), "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
