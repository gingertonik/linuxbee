#pragma once
#include <QString>

// The one place the organiser moves or copies a file on disk.
// Contract: docs/spec/organise.md, O-EXECUTE.
namespace linuxbee::organise {

// Moves or copies one file. Never overwrites: fails if `target` exists.
// Returns an empty text on success, otherwise what went wrong; on failure
// the source is intact and no partial target is left.
QString transferFile(const QString &source, const QString &target, bool move);

}
