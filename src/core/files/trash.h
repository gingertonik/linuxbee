#pragma once
#include <QList>
#include <QString>
#include <QStringList>

// Moving files to the desktop's trash. Contract: docs/spec/delete-files.md
// (FD-x). Nothing here removes a file for good, and nothing here removes
// a folder.
namespace linuxbee {

// Sends one file to the trash and says where it went. The trash folders are
// made first when they are missing. False, and nothing changed, when the
// trash cannot take it. (The organiser uses this for a replaced file.)
bool sendToTrash(const QString &path, QString *pathInTrash);

struct TrashOutcome {
    enum class Status {
        Trashed, // it is in the trash now, at pathInTrash
        NotThere, // nothing is at the path
        Refused, // not a regular file; `reason` says what it is
        Failed, // the trash did not take it; the file is where it was
    };
    QString path; // absolute and clean
    Status status = Status::Failed;
    QString pathInTrash; // Trashed only
    QString reason; // Refused and Failed
};

// FD-TRASH: one outcome for each different path, in the order first given.
QList<TrashOutcome> moveFilesToTrash(const QStringList &paths);

}
