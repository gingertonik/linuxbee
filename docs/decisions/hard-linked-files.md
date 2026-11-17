# Hard-linked files are not tagged

**Decided** 2026-11-17, by the project's owner: "lets go with option 1
and recommend to users NOT to use hard-linked files".

## The question

A music file can have several names (hard links): the usual case is a
library folder linked from a download or seeding folder, so that one
copy of the data serves both.

linuxBee saves tags by writing a whole new version of the file beside
the old one and putting it in the old one's place in one step
(`docs/spec/tag-safe-save.md`). That is what keeps a song whole when a
save is interrupted. It also means the name that was saved now leads
to a new file, and every other name still leads to the old one: the
link is broken without a word, the data is on the disk twice, and the
other folder keeps the old tags.

The other way, writing into the file in place, keeps the names
together and gives up the protection: a save that is interrupted or
runs out of room damages the only copy, under every name at once.

## The decision

- A tag save on a file that has more than one name is **refused,
  before anything is written**. The refusal says how many names the
  file has and how to make it a file of its own.
- linuxBee never writes tags in place, and never splits a link
  silently.
- Playing, browsing and scanning such files are not affected.
- The manual says it plainly: to tag in linuxBee, keep the library as
  plain files; copy, do not link, from a download or seeding folder.

## What it allows

A user who wants to tag one such file makes it a file of its own
first (`cp -p FILE FILE.new && mv FILE.new FILE`); the other names
then keep the old version, which is now the user's choice and not a
side effect.

The other names are not searched for: that means reading a whole file
system for one file. `find FOLDER -samefile FILE` does it when wanted.
