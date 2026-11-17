# Continuous integration

What runs on GitHub when `main` changes. Locked by
`tests/locked/cli_ci.sh`. The workflow is
`.github/workflows/ci.yml`; what its jobs run is `tools/ci.sh`. Both
are locked files: they change by a reviewer's commit only.

## C-WHEN

On every push to `main`, and on every pull request. Never on
`pull_request_target` (that event runs with the repository's rights on
a stranger's code).

## C-RIGHTS

The workflow's token can read the source and nothing else:
`permissions: contents: read` at the top, and no job asks for more. It
uses no secret. This holds for every workflow in the folder but one:
the release workflow's one job may write a Release (release.md,
R-RIGHTS).

## C-NOACTIONS

The workflow uses no action: no `uses:` line at all, not even
`actions/checkout`. An action is someone else's code run with this
repository's token, and pinning it by hash means keeping the hashes up
to date for ever. Here the source is fetched with `git`, and
everything else comes from the distribution's packages. Before adding
an action, change this rule, with the reason.

For the same reason nothing from the event is pasted into a script:
the only `${{ }}` expressions are `matrix.step` and `github.ref` (in
the concurrency group). The commit and the repository are read from
the environment (`GITHUB_SHA`, `GITHUB_REPOSITORY`).

## C-CONTAINER

Every job runs in the Arch Linux `base-devel` image, named by its
digest (`archlinux:base-devel@sha256:` and 64 digits), not by a tag
that can be moved. Arch is what the installer supports
(install.md, I-NEEDS), so what CI builds is what a user builds. The
packages are brought up to date in the job: the digest fixes where the
job starts, the distribution's signed packages fix the rest.

## C-USER

The gate, the tests and the builds run as an ordinary user made in the
job, never as root: tests that take a file's permissions away mean
nothing to root.

## C-STEPS

Three jobs, each one step of `tools/ci.sh`, none stopping the others:

| Step | What |
|---|---|
| `gate` | `tools/check.sh`, as a builder runs it, with a session bus |
| `sanitize` | every test again, built with the address and undefined-behaviour sanitizers; a report from either fails the job |
| `release` | the `release` preset builds, with no test library asked for, and both programs say their version |

The same steps run by hand: in the same image, as root
`CI=true sh tools/ci.sh packages && CI=true sh tools/ci.sh user`, then
as `ci`, `sh tools/ci.sh gate` (or `sanitize`, or `release`).

`packages` and `user` change the system they run on (they install
packages and make a user). They do nothing unless `CI=true` says the
system is a throwaway one; GitHub sets it. The other three refuse to
run as root.

## Not in this version

- **The thread sanitizer.** It was measured (2026-11-17, `main` at
  0.6.0): 34 of the 38 tests that use threads report races, 336 in
  all, of which 23 are in this program's code, every one a hand-over
  through Qt's thread pool or event queue in the convert batch.
  The distribution's Qt is not built for the thread sanitizer, so the
  locks inside it are invisible and every hand-over looks like a race.
  A real race would be lost among them. It needs a Qt built for it.
- **Leaks.** Qt's own at exit drown the program's.
- **Another compiler, another distribution.**
