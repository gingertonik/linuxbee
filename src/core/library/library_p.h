#pragma once
// The library's own state, shared by the two files that implement it
// (library.cpp and scan.cpp). Not a public header: nothing includes it from
// outside src/core/library/.
#include <memory>

#include "library/library.h"
#include "library/store.h"

namespace linuxbee {

// The library is one open store: the file's own SQLite connection, and
// nothing else. L-OPEN keeps it open for the life of the Library object.
struct Library::Private {
    library::Store store;
};

} // namespace linuxbee
