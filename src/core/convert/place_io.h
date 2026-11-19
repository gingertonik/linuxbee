#pragma once
#include <functional>

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

// The three system calls that give a converted file its name (step 9), so that
// a test can have any one of them answer as another disk would, or put a file
// at the name at the very moment of the move, and see that no file is
// replaced. Contract: docs/spec/convert.md, C-PLACE. Nothing but the converter
// and its tests uses this.
namespace linuxbee::convert {

// Each member answers as the system call of its name does: 0, or -1 with errno
// set. A default-made PlaceIo is the real calls.
struct PlaceIo {
    // renameat2 with RENAME_NOREPLACE: the move that never replaces.
    std::function<int(const char *from, const char *to)> renameNoReplace
        = [](const char *from, const char *to) {
              return ::renameat2(AT_FDCWD, from, AT_FDCWD, to, RENAME_NOREPLACE);
          };
    // A second name for the file. It never replaces either.
    std::function<int(const char *from, const char *to)> link
        = [](const char *from, const char *to) { return ::link(from, to); };
    std::function<int(const char *path)> unlink = [](const char *path) { return ::unlink(path); };
};

}
