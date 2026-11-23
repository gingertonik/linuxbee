#pragma once
#include <QString>

// Names that are too long for a scratch name, and names that are not UTF-8.
// Contract: docs/spec/tag-safe-save.md (NM-SCRATCH) and docs/spec/delete-files.md (NM-UTF8).
namespace linuxbee {

// NM-SCRATCH: the file's name as it goes into a scratch name that has `headBytes` bytes before it
// (".lbpart-" + six letters + "-" is 15). A name that fits in 255 bytes with them is returned as
// it is. A longer one becomes: its start, cut between two characters, then "~" and the first
// eight hex digits of the SHA-1 of the whole name's bytes, then its extension (from the last dot)
// when that is 16 bytes or less. The result and `headBytes` together are at most 255 bytes.
QString scratchTail(const QString &fileName, int headBytes);

// NM-UTF8: for a path at which nothing is, whether the first name of it that is not there is the
// stand-in reading (U+FFFD for each bad byte) of an entry in the folder above it whose name is not
// valid UTF-8. `No` also for a path that is there, and for any other reason it is not.
enum class NotUtf8 { No, TheFile, AFolder };
NotUtf8 notUtf8Where(const QString &path);

// The words for TheFile and AFolder (an empty string for No).
QString notUtf8Words(NotUtf8 where);

}
