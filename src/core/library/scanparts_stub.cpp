#include "library/library.h"

// STUB (plan 36a). The builder gives these real bodies in the library's
// own files and removes this file.
namespace linuxbee {

ScanFindings findForScan(const QStringList &, const ScanKnown &, const ScanFindOptions &)
{
    return { };
}

ScanReport addedUp(const ScanReport &a, const ScanReport &)
{
    return a;
}

ScanKnown Library::scanKnown() const
{
    return { };
}

ScanReport Library::applyScan(const ScanFindings &, const ScanOptions &)
{
    return { };
}

}
