#pragma once
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

// Moving files to the desktop's trash. Contract: docs/spec/delete-files.md
// (FD-x). Nothing here removes a file for good, and nothing here removes
// a folder.
namespace linuxbee {

// Sends one file to the trash and says where it went. The trash folders are
// made first when they are missing. False, and nothing changed, when the
// trash cannot take it. (The organiser uses this for a replaced file.)
bool sendToTrash(const QString &path, QString *pathInTrash);

// FD-SEND, FD-STAYED: what became of one file.
enum class SendResult {
    Went, // nothing is at the path any more
    NotTaken, // not tried, or the trash said no; the file is where it was
    StillThere, // the trash said "moved" and the file is where it was
};
// What moves a file into the trash: the real path, and where it went. Given so
// that a test can stand in for the desktop library; an empty one is the library.
using TrashMover = std::function<bool(const QString &realPath, QString *pathInTrash)>;
// sendToTrash is this with the desktop library, and true for Went. `pathInTrash`
// is written for Went only.
SendResult sendToTrashWith(const QString &path, QString *pathInTrash, const TrashMover &mover);

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
QList<TrashOutcome> moveFilesToTrashWith(const QStringList &paths, const TrashMover &mover);

}
