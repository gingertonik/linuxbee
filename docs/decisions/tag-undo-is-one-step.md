# Undo for tags is one step

**Status:** Decided by the owner, 2026-11-16.

## The question

How far back a tag change can be undone.

## The decision

- The last tag change can be undone, until the next one is made or the
  program closes.
- Pictures it removed are kept in a scratch folder so that they come back
  too.

## What it allows

A wrong change is taken back at once, without a history to keep.

## Where it is relied on

- `docs/spec/tag-undo.md`
