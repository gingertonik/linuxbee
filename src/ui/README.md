# src/ui

The Qt Widgets half of linuxBee, built as the static library `linuxbee_ui`.
`src/app` is a thin `main()` over it.

Rules of the house:

- Dependencies point one way: `src/app` uses `src/ui`, which uses
  `src/core`. The gate refuses a core or command-line file that includes a
  header from here.
- Whatever can be decided without a window is decided in `src/core` and
  tested there. A class in `src/ui` shows state and forwards what the user
  did; it does not hold rules.
- Each part lives in its own folder (`src/ui/<part>/`). Files are found by
  pattern, so adding one needs no build-file edit. Includes are written
  from this folder: `#include "<part>/<file>.h"`.
- A part that needs another Qt module puts a `deps.cmake` next to its
  sources, as in `src/core`:

  ```cmake
  find_package(Qt6 6.5 REQUIRED COMPONENTS Multimedia)
  target_link_libraries(linuxbee_ui PUBLIC Qt6::Multimedia)
  ```

- Every widget a test or a person needs to find has an object name. Names
  are lower camel case and are part of each plan's contract.
- Text the user reads goes through `tr()`.
- Tests named `tst_ui_*` link this library and run with
  `QT_QPA_PLATFORM=offscreen`, so they need no display.
