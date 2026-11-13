# Rules made ready for many tracks

An auto-playlist asks one rule set about every track of the library.
`matchesRules` (rules.md) checks the set and builds its regular
expressions anew for each track; that is right for one question and
wasteful for a hundred thousand. `CompiledRules` does that work once.

This document is the contract for `src/core/rules/compiledrules.h`. The
locked test `tests/locked/tst_compiledrules.cpp` checks it. It adds no
rule about what matches: rules.md decides that, alone.

## Sources

| Tag | Source |
|---|---|
| RC-… | A linuxBee decision, after plan 06's review (regular expressions were compiled once per track). |

## RC-VALID

`CompiledRules(rules)` checks the set once. `problems()` is what
`validateRules(rules)` returns; `isValid()` is true when that is empty.
An object made without a set is not valid and has no problems to tell.

## RC-MATCH

For every rule set, track and context:

`CompiledRules(rules).matches(track, context)` is
`matchesRules(rules, track, context)`.

In particular an object that is not valid matches nothing, and a valid
set with no rules matches every track.

`matchesRules` itself keeps its meaning and stays a function that
remembers nothing between calls.

## RC-PATTERNS

Every regular expression of the set (the first value of each rule whose
operator is `matches` or `does-not-match`, sub-rules included) is built
once, when the object is made, and used for every track after that.
`patternCount()` is how many are held: one for each different expression
text; none when the set is not valid.

Making the object is the expensive step and matching is the cheap one:
asking one object about many tracks must take under a fifth of the time
`matchesRules` takes for the same questions, for a set of six regular
expressions. (The locked test measures exactly that.)

## RC-COPY

Copying an object is cheap and the copies are independent of the
original's lifetime. An object never changes after it is made, so one
object may be asked from several threads at once.

## RC-HOSTILE

A regular expression that could run for a very long time on a long value
(`(a+)+$` against many `a` and one `b`) comes back in bounded time with
"no match", as it does in `matchesRules`: the expression engine's own
limit ends it. Neither function hangs.

## Not in this version

- Moving the auto-playlists and the organiser onto this class: each does
  it in its own plan when it is next opened.
- A cache inside `matchesRules`.
