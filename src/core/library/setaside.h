#pragma once
#include <QString>
#include <QStringList>

// L-SETASIDE: what Organise set aside and a kill left behind. Private to the library and its tests.
namespace linuxbee::setaside {

struct Swept {
    QStringList putBack; // the real paths of the files given back their names, ascending
    QStringList kept; // the waiting paths of the files that stay, ascending
};

// Looks below each of `folders` (clean paths), never through a link and never into a hidden
// folder, for folders named exactly `.linuxbee-replaced-N` (N from 1 to 9999, as Organise writes
// it). Each regular file directly in one, whose last change is older than the age
// (LINUXBEE_LEFTOVER_AGE in seconds, 600 when it is not named), is given back its name in the
// folder above by the never-replacing move of O-SPARE; when the name is taken it stays and is
// kept. A waiting folder left empty is taken away, and nothing else is ever removed.
Swept sweepSetAside(const QStringList &folders);

// The path a file in a waiting folder had before it was set aside: the same name in the folder
// above the waiting folder.
QString realPathOf(const QString &waitingPath);

}
