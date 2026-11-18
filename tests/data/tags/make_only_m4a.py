#!/usr/bin/env python3
"""Builds the fixture of "a save changes only what it names" for MPEG-4.

Run from the repository root:   python3 -I tests/data/tags/make_only_m4a.py

Needs the Python package mutagen, a C++ compiler and the TagLib headers. The
fixture is committed; this is kept as the record of exactly what it holds.

The file is written by mutagen (a tagging library that is not the one the
program uses) and then saved once, with no change, by TagLib. TagLib writes
the items of a file in the order of their names, whatever is saved, so a file
it has never written is put in another order at its first save. A file it
has written once does not change again. The test compares bytes, so it
starts from the settled file.

  only.m4a   basic.m4a with 25 items: atoms of every kind the sample corpus
             has, and freeform items named as other taggers name them, in
             small letters and mixed
"""
import os
import random
import shutil
import subprocess
import tempfile

from mutagen.mp4 import MP4, MP4Cover, MP4FreeForm

HERE = os.path.dirname(os.path.abspath(__file__))

SETTLE = r"""
#include <taglib/mp4file.h>
using namespace TagLib;
int main(int, char **argv)
{
    MP4::File f(argv[1], false);
    if (!f.isValid() || !f.hasMP4Tag())
        return 2;
    return f.save() ? 0 : 1;
}
"""

ITUNES = "----:com.apple.iTunes:"


def jpeg(size, seed):
    return b"\xff\xd8\xff\xe0" + random.Random(seed).randbytes(size) + b"\xff\xd9"


def text(value):
    return [MP4FreeForm(value.encode("utf-8"))]


def main():
    work = tempfile.mkdtemp()
    source = os.path.join(work, "settle.cpp")
    settle = os.path.join(work, "settle")
    with open(source, "w") as f:
        f.write(SETTLE)
    flags = subprocess.run(["pkg-config", "--cflags", "--libs", "taglib"], check=True, capture_output=True, text=True).stdout.split()
    subprocess.run(["c++", "-O1", source, "-o", settle] + flags, check=True)
    target = os.path.join(HERE, "only.m4a")
    shutil.copyfile(os.path.join(HERE, "basic.m4a"), target)
    m = MP4(target)
    m.delete()
    m = MP4(target)
    m["\xa9nam"] = ["Old Title"]
    m["\xa9ART"] = ["Artist A"]
    m["\xa9alb"] = ["Album"]
    m["aART"] = ["Album Artist"]
    m["\xa9gen"] = ["Rock"]
    m["\xa9day"] = ["2001-05-12"]
    m["\xa9wrt"] = ["A Composer"]
    m["\xa9lyr"] = ["m4a lyrics"]
    m["\xa9cmt"] = ["A comment"]
    m["trkn"] = [(3, 12)]
    m["disk"] = [(1, 2)]
    m["tmpo"] = [120]
    m["cpil"] = True
    m["rtng"] = [0]  # the advisory rating of a shop, not the star rating
    m["soar"] = ["A, Artist"]
    m["covr"] = [MP4Cover(jpeg(3000, 1), MP4Cover.FORMAT_JPEG), MP4Cover(b"\x89PNG\r\n\x1a\n" + random.Random(2).randbytes(2000), MP4Cover.FORMAT_PNG)]
    # Freeform items as another tagger names them: mixed case and small letters.
    m[ITUNES + "MusicBrainz Track Id"] = text("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee")
    m[ITUNES + "replaygain_track_gain"] = text("-5.00 dB")
    m[ITUNES + "replaygain_track_peak"] = text("0.988")
    m[ITUNES + "My Custom Thing"] = text("custom value")
    m[ITUNES + "iTunNORM"] = text(" 00000A00 00000A00")
    m[ITUNES + "MOOD"] = [MP4FreeForm(b"Calm"), MP4FreeForm(b"Bright")]
    # One key under two names: what a save by a linuxBee before this rule left in a file.
    m[ITUNES + "publisher"] = text("A Label")
    m[ITUNES + "PUBLISHER"] = text("A Label")
    # Not Apple's name space: the tag library reads no key from it.
    m["----:org.example:foo"] = text("another mean")
    m.save(padding=lambda info: 1024)
    subprocess.run([settle, target], check=True)
    print("only.m4a", os.path.getsize(target))
    shutil.rmtree(work)


if __name__ == "__main__":
    main()
