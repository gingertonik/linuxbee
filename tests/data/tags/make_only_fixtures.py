#!/usr/bin/env python3
"""Builds the fixtures of "a save changes only what it names" for ID3v2.

Run from the repository root:   python3 -I tests/data/tags/make_only_fixtures.py

Needs the Python package mutagen, a C++ compiler and the TagLib headers. The
fixtures are committed; this is kept as the record of exactly what each file
holds.

Each file is written by mutagen (a tagging library that is not the one the
program uses) and then saved once, with no change, by TagLib. That second
step matters: TagLib writes a text frame without the zero mutagen ends it
with, so a file TagLib has never written changes in most of its frames at
its first save, whatever is saved. A file it has written once does not
change again. The tests compare bytes, so they start from the settled file.
The settling save also writes the ID3v1 tag from the ID3v2 tag's values, as
every save of the program does.

  only24.mp3   ID3v2.4, 47 frames of every kind the audit of 2026-11-17 met
  only23.mp3   the same content as ID3v2.3 (44 frames: ID3v2.3 has no place for three)
  onlyape.mp3  only24.mp3 with an APEv2 tag between the sound and the ID3v1 tag
"""
import os
import random
import shutil
import subprocess
import tempfile

from mutagen.apev2 import BINARY, APEv2, APEValue
from mutagen.id3 import (APIC, CHAP, COMM, CTOC, ETCO, GEOB, ID3, MCDI, PCNT, POPM, PRIV, RVA2, SYLT, TALB,
                         TBPM, TCMP, TCON, TDOR, TDRC, TIPL, TIT1, TIT2, TKEY, TMCL, TMED, TMOO, TPE1, TPOS,
                         TPUB, TRCK, TSOP, TXXX, UFID, USLT, WOAR, WXXX, CTOCFlags)

HERE = os.path.dirname(os.path.abspath(__file__))

SETTLE = r"""
#include <taglib/id3v1tag.h>
#include <taglib/id3v2header.h>
#include <taglib/id3v2tag.h>
#include <taglib/mpegfile.h>
#include <taglib/tpropertymap.h>
using namespace TagLib;
int main(int, char **argv)
{
    MPEG::File f(argv[1], false);
    if (!f.isValid() || !f.hasID3v2Tag())
        return 2;
    const int v = f.ID3v2Tag()->header()->majorVersion();
    // The ID3v1 tag as a save leaves it: what it can hold of the ID3v2 tag's values.
    f.ID3v1Tag(true)->setProperties(f.ID3v2Tag()->properties());
    // An APEv2 tag, where there is one, is written again too.
    const int kinds = f.hasAPETag() ? MPEG::File::AllTags : (MPEG::File::ID3v2 | MPEG::File::ID3v1);
    return f.save(kinds, File::StripNone, v == 3 ? ID3v2::v3 : ID3v2::v4, File::Duplicate) ? 0 : 1;
}
"""


def jpeg(size, seed):
    # A JPEG's first and last bytes round fixed filler; readers treat it as opaque data.
    return b"\xff\xd8\xff\xe0" + random.Random(seed).randbytes(size) + b"\xff\xd9"


def frames():
    t = ID3()
    t.add(TIT2(encoding=3, text=["Old Title"]))
    t.add(TPE1(encoding=3, text=["Artist A", "Artist B"]))
    t.add(TALB(encoding=3, text=["Album"]))
    t.add(TCON(encoding=3, text=["Rock", "Jazz"]))
    t.add(TRCK(encoding=3, text=["3/12"]))
    t.add(TPOS(encoding=3, text=["1/2"]))
    # Two comments with no description, in two languages; and one with a description, which is another thing.
    t.add(COMM(encoding=3, lang="eng", desc="", text=["English comment"]))
    t.add(COMM(encoding=3, lang="deu", desc="", text=["Deutscher Kommentar"]))
    t.add(COMM(encoding=3, lang="eng", desc="iTunNORM", text=[" 00000A00 00000A00"]))
    t.add(USLT(encoding=3, lang="eng", desc="", text="English lyrics line"))
    t.add(USLT(encoding=3, lang="fra", desc="", text="Paroles francaises"))
    t.add(SYLT(encoding=3, lang="eng", format=2, type=1, desc="", text=[("la", 1000), ("la", 2000)]))
    t.add(TXXX(encoding=3, desc="replaygain_track_gain", text=["-7.50 dB"]))
    t.add(TXXX(encoding=3, desc="replaygain_track_peak", text=["0.988"]))
    t.add(TXXX(encoding=3, desc="MusicBrainz Album Id", text=["11111111-2222-3333-4444-555555555555"]))
    t.add(TXXX(encoding=3, desc="My Custom Thing", text=["custom value"]))
    # A user text frame beside the standard frame of the same key: three pairs the audit's census met.
    t.add(TMOO(encoding=3, text=["Calm"]))
    t.add(TXXX(encoding=3, desc="MOOD", text=["Calm"]))
    t.add(TPUB(encoding=3, text=["A Label"]))
    t.add(TXXX(encoding=3, desc="Label", text=["Another Label"]))
    t.add(TIT1(encoding=3, text=["A Grouping"]))
    t.add(TXXX(encoding=3, desc="WORK", text=["A Work"]))
    t.add(TMED(encoding=3, text=["CD", "Digital Media"]))
    # One frame that holds many keys: the people involved, with a role no table knows.
    t.add(TIPL(encoding=3, people=[["producer", "A Producer"], ["engineer", "An Engineer"], ["tea", "A Tea Maker"]]))
    t.add(TMCL(encoding=3, people=[["guitar", "A Guitarist"]]))
    t.add(UFID(owner="http://musicbrainz.org", data=b"aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"))
    t.add(PRIV(owner="WM/MediaClassPrimaryID", data=b"\x01\x02\x03\x04"))
    t.add(GEOB(encoding=3, mime="application/octet-stream", filename="x.bin", desc="blob", data=b"\x00" * 64))
    t.add(RVA2(desc="track", channel=1, gain=-3.5, peak=0.9))
    t.add(CHAP(element_id="ch0", start_time=0, end_time=500, sub_frames=[TIT2(encoding=3, text=["Chapter 1"])]))
    t.add(CHAP(element_id="ch1", start_time=500, end_time=1000, sub_frames=[TIT2(encoding=3, text=["Chapter 2"])]))
    t.add(CTOC(element_id="toc", flags=CTOCFlags.TOP_LEVEL | CTOCFlags.ORDERED, child_element_ids=["ch0", "ch1"],
               sub_frames=[TIT2(encoding=3, text=["TOC"])]))
    t.add(APIC(encoding=3, mime="image/jpeg", type=3, desc="front", data=jpeg(3000, 1)))
    t.add(APIC(encoding=3, mime="image/jpeg", type=4, desc="back", data=jpeg(2000, 2)))
    t.add(POPM(email="MusicBee", rating=196, count=7))
    t.add(POPM(email="Windows Media Player 9 Series", rating=255))  # no counter, as that player writes it
    t.add(TDRC(encoding=3, text=["2001-05-12"]))
    t.add(TDOR(encoding=3, text=["1999-03"]))
    t.add(WXXX(encoding=3, desc="homepage", url="http://example.com/"))
    t.add(WOAR(url="http://artist.example/"))
    t.add(TCMP(encoding=3, text=["1"]))
    t.add(TSOP(encoding=3, text=["A, Artist"]))
    t.add(MCDI(data=b"\x00\x01\x02\x03"))
    t.add(PCNT(count=42))
    t.add(ETCO(format=2, events=[(3, 1000)]))
    t.add(TBPM(encoding=3, text=["120"]))
    t.add(TKEY(encoding=3, text=["Am"]))
    return t


def main():
    work = tempfile.mkdtemp()
    source = os.path.join(work, "settle.cpp")
    settle = os.path.join(work, "settle")
    with open(source, "w") as f:
        f.write(SETTLE)
    flags = subprocess.run(["pkg-config", "--cflags", "--libs", "taglib"], check=True, capture_output=True, text=True).stdout.split()
    subprocess.run(["c++", "-O1", source, "-o", settle] + flags, check=True)
    for name, version in (("only24.mp3", 4), ("only23.mp3", 3)):
        target = os.path.join(HERE, name)
        shutil.copyfile(os.path.join(HERE, "basic.mp3"), target)
        try:
            ID3(target).delete()
        except Exception:
            pass
        t = frames()
        if version == 3:
            t.update_to_v23()
        t.save(target, v2_version=version, v1=2, padding=lambda info: 1024)
        subprocess.run([settle, target], check=True)
        print(name, os.path.getsize(target))
    # The third file: the ID3v1 tag is cut off, an APEv2 tag is put at the end by mutagen, and the
    # settling save writes the tags again and an ID3v1 tag after them.
    target = os.path.join(HERE, "onlyape.mp3")
    with open(os.path.join(HERE, "only24.mp3"), "rb") as f:
        data = f.read()
    assert data[-128:-125] == b"TAG"
    with open(target, "wb") as f:
        f.write(data[:-128])
    ape = APEv2()
    ape["Title"] = "Ape Title"
    ape["Artist"] = "Ape Artist"
    ape["REPLAYGAIN_TRACK_GAIN"] = "-7.50 dB"
    ape["MP3GAIN_MINMAX"] = "089,210"
    ape["Cover Art (Front)"] = APEValue(b"cover.jpg\x00" + jpeg(200, 3), BINARY)
    ape.save(target)
    subprocess.run([settle, target], check=True)
    print("onlyape.mp3", os.path.getsize(target))
    shutil.rmtree(work)


if __name__ == "__main__":
    main()
