# linuxbee_core

One folder per subsystem. QtCore only: no QtGui, no QtWidgets.

- Every `.cpp` and `.h` under `src/core/` is compiled into the library.
  Adding a file needs no build-file edit.
- A subsystem that needs an outside library adds
  `src/core/<subsystem>/deps.cmake` holding its `find_package(...)` and
  `target_link_libraries(linuxbee_core PUBLIC ...)` lines. The build file
  includes every `deps.cmake` it finds.
- Headers are included by path from this directory:
  `#include "template/template.h"`.
