#!/usr/bin/env python3
"""Builds the Love fixtures from the basic ones in this directory.

Run from the repository root:   python3 -I tests/data/tags/make_love_fixtures.py

Needs the Python package mutagen. The fixtures are committed; this is kept as
the record of exactly what each file holds. Every value a test expects is
written here, by a tagging library that is not the one the program uses.

Where the names come from (docs/spec/tag-reading.md, R-LOVE): the ID3v2 and
MPEG-4 forms are as found in files MusicBee had written; the Vorbis form is
the one a MusicBee forum post gives.
"""
import os
import shutil

from mutagen.flac import FLAC
from mutagen.id3 import ID3, TXXX
from mutagen.mp4 import MP4, MP4FreeForm
from mutagen.oggvorbis import OggVorbis

HERE = os.path.dirname(os.path.abspath(__file__))


def copy(source, name):
    target = os.path.join(HERE, name)
    shutil.copyfile(os.path.join(HERE, source), target)
    return target


def main():
    p = copy("basic.mp3", "love.mp3")
    tags = ID3(p)
    tags.add(TXXX(encoding=3, desc="LOVE RATING", text=["L"]))
    tags.save(p, v2_version=4)

    f = FLAC(copy("basic.flac", "love.flac"))
    f["LOVE RATING"] = ["L"]
    f.save()

    f = OggVorbis(copy("basic.ogg", "love.ogg"))
    f["LOVE RATING"] = ["L"]
    f.save()

    f = MP4(copy("basic.m4a", "love.m4a"))
    f["----:com.apple.iTunes:LOVERATING"] = [MP4FreeForm(b"L")]
    f.save()

    f = FLAC(copy("basic.flac", "ban.flac"))
    f["LOVE RATING"] = ["B"]
    f.save()

    # Lower case, with a space after it: still Love.
    f = FLAC(copy("basic.flac", "love-lower.flac"))
    f["LOVE RATING"] = ["l "]
    f.save()

    # Not a Love value: the field is absent and the key stays among the other tags.
    f = FLAC(copy("basic.flac", "love-odd.flac"))
    f["LOVE RATING"] = ["Yes"]
    f.save()

    # Both names at once: the first of the row wins, the other stays among the other tags.
    f = FLAC(copy("basic.flac", "love-both.flac"))
    f["LOVE RATING"] = ["L"]
    f["LOVERATING"] = ["B"]
    f.save()

    # The first name holds something that is not a Love value, the second holds one: the second gives
    # the field, and the first stays among the other tags.
    f = FLAC(copy("basic.flac", "love-second.flac"))
    f["LOVE RATING"] = ["Yes"]
    f["LOVERATING"] = ["L"]
    f.save()


if __name__ == "__main__":
    main()
