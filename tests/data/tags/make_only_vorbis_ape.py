#!/usr/bin/env python3
"""Builds the fixtures of "a save changes only what it names" for Vorbis comments and APEv2.

Run from the repository root:   python3 -I tests/data/tags/make_only_vorbis_ape.py

Needs the Python package mutagen, a C++ compiler and the TagLib headers. The
fixtures are committed; this is kept as the record of exactly what they hold.

Each file is written by mutagen (a tagging library that is not the one the
program uses) and then saved once, with no change, by TagLib. At its first
save of a file, whatever is saved, TagLib writes the names of Vorbis comments
in capitals and puts comments and APEv2 items in the order of their names. A
file it has written once does not change again. The test compares bytes, so
it starts from the settled files.

  only.flac   basic.flac with 13 comments, two of one name among them, and its picture
  only.ogg    basic.ogg with the same comments and two pictures (they are comments there)
  only.opus   basic.opus, the same
  only.wv     basic.wv with 11 APEv2 items named as other taggers name them (first letter
              in capitals, small letters, mixed), a cover, another binary item and a link
"""
import base64
import os
import shutil
import subprocess
import tempfile

from mutagen.apev2 import BINARY, EXTERNAL, APEValue
from mutagen.flac import FLAC, Picture
from mutagen.oggopus import OggOpus
from mutagen.oggvorbis import OggVorbis
from mutagen.wavpack import WavPack

HERE = os.path.dirname(os.path.abspath(__file__))

SETTLE = r"""
#include <taglib/fileref.h>
using namespace TagLib;
int main(int, char **argv)
{
    FileRef f(argv[1], false);
    if (f.isNull())
        return 2;
    return f.file()->save() ? 0 : 1;
}
"""

COMMENTS = [
    ("title", ["Old Title"]),
    ("artist", ["Artist A", "Artist B"]),
    ("album", ["Album"]),
    ("date", ["2001"]),
    ("tracknumber", ["3"]),
    ("genre", ["Rock"]),
    ("composer", ["A Composer"]),
    ("comment", ["A comment"]),
    ("lyrics", ["la la"]),
    ("My Custom Thing", ["custom value"]),
    ("replaygain_track_gain", ["-5.00 dB"]),
    ("musicbrainz_trackid", ["aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"]),
]


def picture(kind, seed):
    p = Picture()
    p.type = kind
    p.mime = "image/jpeg"
    p.desc = "a picture"
    p.data = b"\xff\xd8\xff\xe0" + bytes((i * 7 + seed) % 251 for i in range(300)) + b"\xff\xd9"
    return p


def vorbis(cls, source, target, pictures_as_comments):
    shutil.copyfile(os.path.join(HERE, source), target)
    f = cls(target)
    f.delete()
    f = cls(target)
    for name, values in COMMENTS:
        f[name] = values
    if pictures_as_comments:
        f["metadata_block_picture"] = [base64.b64encode(picture(k, k).write()).decode("ascii") for k in (3, 4)]
    else:
        f.clear_pictures()
        f.add_picture(picture(3, 3))
    f.save()


def wavpack(target):
    shutil.copyfile(os.path.join(HERE, "basic.wv"), target)
    w = WavPack(target)
    w.delete()
    w = WavPack(target)
    w.add_tags()
    w["Title"] = "Old Title"
    w["Artist"] = ["Artist A", "Artist B"]
    w["Album"] = "Album"
    w["Year"] = "2001"
    w["Track"] = "3/12"
    w["Composer"] = "A Composer"
    w["mixed Case"] = "mixed"
    w["replaygain_track_gain"] = "-5.00 dB"
    w["Cover Art (Front)"] = APEValue(b"cover.jpg\x00" + picture(3, 3).data, BINARY)
    w["Bin"] = APEValue(b"\x00\x01\x02\x03", BINARY)
    w["Link"] = APEValue("http://example.invalid/", EXTERNAL)
    w.save()


def main():
    work = tempfile.mkdtemp()
    source = os.path.join(work, "settle.cpp")
    settle = os.path.join(work, "settle")
    with open(source, "w") as f:
        f.write(SETTLE)
    flags = subprocess.run(["pkg-config", "--cflags", "--libs", "taglib"], check=True, capture_output=True, text=True).stdout.split()
    subprocess.run(["c++", "-O1", source, "-o", settle] + flags, check=True)
    made = []
    for cls, ext, as_comments in ((FLAC, "flac", False), (OggVorbis, "ogg", True), (OggOpus, "opus", True)):
        target = os.path.join(HERE, "only." + ext)
        vorbis(cls, "basic." + ext, target, as_comments)
        made.append(target)
    target = os.path.join(HERE, "only.wv")
    wavpack(target)
    made.append(target)
    for target in made:
        subprocess.run([settle, target], check=True)
        print(os.path.basename(target), os.path.getsize(target))
    shutil.rmtree(work)


if __name__ == "__main__":
    main()
