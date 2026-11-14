# Converting many files

The converter (convert.md) turns one file into another. This part turns a
selection into jobs, runs a few of them at a time in the background, and
tells its listener what is happening.

This document is the contract for `src/core/convert/convertbatch.h`. The
locked test `tests/locked/tst_convertbatch.cpp` checks it.

## Sources

| Tag | Source |
|---|---|
| W-CF | MusicBee wiki, "Convert Format" (converting a selection; "the maximum number of files to convert in parallel"): https://musicbee.fandom.com/wiki/Convert_Format |
| B-… | A linuxBee decision. |

Read on 2026-11-12. The wiki documents that a selection is converted to a
chosen folder or alongside the originals, several files at a time. How
names are kept apart is not documented.

## B-DEST

`planBatch(sources, options)` returns one job per source, in order. It is
pure: it does not look at the disk.

- Each source path is made absolute and clean.
- The result's folder is `options.outputFolder` (trimmed, absolute,
  clean), or the source's own folder when that is empty.
- The result's name is the source's name with its last extension replaced
  by the format's (`fileExtension`). A name with no extension keeps all of
  it and gains one.
- No job may write to a path that is one of the sources, or the
  destination of an earlier job. A job that would gets `_1` before the
  extension, or `_2`, and so on: the first that is free. Paths are
  compared exactly, case included.
- The settings, `ifExists`, `copyTags`, `copyPictures`, the tag options
  and the ffmpeg path are copied into every job.

What is on the disk already is the converter's business when the job runs
(`ifExists`).

## B-PARALLEL

A batch runs at most `maxParallel()` jobs at once. The default is the
number of processor threads, at least 1 and at most 4. `setMaxParallel`
takes 1 to 16, pulling other numbers into that range, and is ignored
during a run. With 1, jobs run in order.

## B-RUN

`start(jobs)` begins a run and returns at once. It returns false and
starts nothing when `jobs` is empty or a run is under way.

- The converter is called once per job, never on the thread the batch
  object lives on.
- `jobStarted(i)` is emitted when job i begins; `jobProgress(i, fraction)`
  as it reports progress, the fraction from 0 to 1 and never going
  backwards; `jobFinished(i, result)` once for every job; `finished()`
  once, after the last `jobFinished`.
- All signals are emitted on the batch object's thread, so a listener
  needs no locking.
- A job that fails does not stop the others.
- `results()` has one entry per job, by index; by the time of
  `finished()` it is complete. `isRunning()` is true from `start` until
  just before `finished()` is emitted.

The converter is `convertFile` unless `setConverter` replaced it (ignored
during a run).

## B-CANCEL

`cancel()` asks a run to stop:

- a job that has not begun never calls the converter; it finishes with
  status `Cancelled` and no `jobStarted`;
- a job under way is told through its progress function, which returns
  false from then on; the converter ends it as it sees fit;
- `finished()` is still emitted, after every job has had its
  `jobFinished`.

The batch can be started again afterwards. `cancel()` with nothing running
does nothing.

Destroying the batch cancels a run and waits until no job is working.

## B-PART

The file ffmpeg writes to while a job runs is named as convert.md,
C-CLEAN says (`.lbpart-XXXXXX-NAME`), so it can never be one of the
sources or destinations of the batch, whatever they are called.

## Not in this version

- Naming results by a template, and keeping the folder structure under
  the output folder.
- Pausing; changing the number of parallel jobs during a run.
- A `linuxbeectl` command for a whole selection.

## Open parity questions

| Rule | Question for someone with MusicBee |
|---|---|
| B-DEST | What two files with the same name, converted into one folder, are called. |
| B-PARALLEL | The default number of parallel conversions. |
