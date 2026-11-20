# An ID3v2.3 save does not move what it cannot keep

**Status:** Decided by the owner, 2026-11-16.

## The question

An ID3v2.3 tag has no frame for some values an ID3v2.4 tag holds. A save
could move them into frames of free text, or say that they were not kept.

## The decision

- They are listed as not kept, and the save says so. Nothing is moved into
  a `TXXX` frame.

## What it allows

No value is put where another program would not look for it.

## Where it is relied on

- `docs/spec/tag-writing.md`, the values not kept
