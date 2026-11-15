# The scan runner

A scan of a music folder that leaves the window free: the looking at
files happens on another thread, the storing on the library's own.
`ScanRunner` in `src/ui/shell/scanrunner.h`. It owns no rule of what a
scan is: that is library.md (L-SCAN, L-PARTS).

## SR-THREADS

`ScanRunner(library, hooks)` lives on the thread it was made on, which
must be the library's.

For each scan it takes `Library::scanKnown()` on its own thread, runs
`findForScan` on another thread, and calls `Library::applyScan` on its
own thread for each part and for the answer (L-PARTS, "In parts"). No
function of the library is ever called from the other thread. All
signals are emitted on the runner's own thread.

`hooks.looking(done, total)`, when set, is called on the looking thread
before each file, with the values of the progress function, and before
the runner looks whether it was told to stop (so a stop asked for while
the hook is held takes effect when the hook returns, and that file is
not looked at). It is there for tests, and to slow a scan down by hand.

## SR-SCAN

`scan(folders, options)` asks for a scan and returns at once.
`options.category` and `options.now` are used as in L-SCAN;
`options.progress` is not used.

- With nothing running, the scan begins before `scan` returns:
  `started` is emitted from within the call. Otherwise it waits.
- Scans run one at a time, in the order asked. `isRunning()` is true
  from the call until the last scan asked for has finished; `waiting()`
  is how many have not started.
- `started(folders)` when a scan begins; `finished(report, folders)`
  when everything it found is stored, with the report of the whole scan
  (the parts' reports added up, L-PARTS); `idle()` after the `finished`
  of the last one.
- A scan the library would refuse (an invalid `now`, an unknown
  category) looks at no file: `started`, then `finished` with an empty
  report.
- The findings are handed over every `partSize()` files (200 unless
  `setPartSize` says otherwise; below 1 counts as 1) and stored as they
  arrive, so tracks appear in the library while a long scan runs.
- `progress(done, total)` says how far the looking has got. It is
  emitted on the runner's thread, never with a smaller `done` than
  before within one scan, and, for a scan that was not stopped, a last
  time with `done` equal to `total` before `finished`. The runner emits that last one itself when the looking did not report
  it. It need not be
  emitted for every file.

## SR-STOP

`cancel()` stops the scan under way and forgets those waiting. What the
stopped scan had already handed over stays stored; it ends with
`finished` and a report whose `cancelled` is true. The forgotten ones
get no signal. `idle()` follows. With nothing running, `cancel()` does
nothing.

Destroying the runner stops the looking and waits for that thread to
end. Nothing is stored and no signal is emitted from the destructor on.

## SR-SHELL

The library shell has one runner, `LibraryShell::scanner()`, and "Scan
Folders" goes through it (library-shell.md, SH-COMMANDS):

- the command returns as soon as the scan is asked for, with the
  message `Scan started`;
- each `progress(done, total)` sets the message `Scanning: DONE of TOTAL
  files`;
- `finished` sets the message SH-COMMANDS gives for a scan, with
  ` (stopped)` after it when the report's `cancelled` is true.

Asked for again while a scan runs, the command queues another.

## Not in this version

- A command, a button or a key to stop a scan (the runner can; the
  window does not offer it yet).
- The folder monitor's checks, which still run on the window's thread.
- A progress bar.

## Open parity questions

| Rule | Question |
|---|---|
| SR-SHELL | What MusicBee shows while it scans, and whether the tracks appear as they are found. linuxBee: a status line; yes. |
