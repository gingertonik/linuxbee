#pragma once
#include "organiser.h"

// What the organiser does after its planned moves: the other files (O-OTHER)
// and the folders the moves left empty (O-EMPTY). Private to the organiser;
// executeOrganise calls both. Contract: docs/spec/organise.md.
namespace linuxbee::organise {

// O-OTHER. Moves the remaining non-music files of each folder a track was
// moved out of, when that folder's tracks went to one other folder, and adds
// an outcome for each file, flagged otherFile. Reads the planned outcomes
// already in `report`.
void takeAlongOtherFiles(OrganiseReport *report);

// O-EMPTY. Removes each folder a track was moved out of if it is now empty,
// then its parent, and so on upward, stopping at the first that still holds
// something. A folder is removed only if it lies strictly below a floor: the
// non-empty `sourceRoots`, or, with none, the deepest folder holding every
// moved track. Lists the removed folders in `report->removedFolders`.
void removeEmptiedFolders(OrganiseReport *report, const QStringList &sourceRoots);

}
