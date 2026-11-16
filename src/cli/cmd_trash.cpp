#include <QTextStream>

#include "command.h"
#include "files/trash.h"

namespace {

const char *const kUsage = "usage: linuxbeectl trash FILE...";

// FD-CLI: one line for each outcome, in order. Exit 0 when every file went to the trash, 3 when
// at least one did not, 2 for no file or for an option (there are none: a word that starts with a
// dash is a mistake, never a file name).
int runTrash(const QStringList &arguments)
{
    if (arguments.isEmpty())
        return linuxbee::cli::usageError(QLatin1String(kUsage));
    for (const QString &argument : arguments) {
        if (argument.startsWith(QLatin1Char('-')))
            return linuxbee::cli::usageError(QLatin1String(kUsage));
    }

    using Status = linuxbee::TrashOutcome::Status;
    QTextStream out(stdout);
    bool all = true;
    for (const linuxbee::TrashOutcome &outcome : linuxbee::moveFilesToTrash(arguments)) {
        switch (outcome.status) {
        case Status::Trashed:
            out << QStringLiteral("trashed: %1 -> %2").arg(outcome.path, outcome.pathInTrash)
                << '\n';
            break;
        case Status::NotThere:
            all = false;
            out << QStringLiteral("not there: %1").arg(outcome.path) << '\n';
            break;
        case Status::Refused:
            all = false;
            out << QStringLiteral("refused: %1: %2").arg(outcome.path, outcome.reason) << '\n';
            break;
        case Status::Failed:
            all = false;
            out << QStringLiteral("failed: %1: %2").arg(outcome.path, outcome.reason) << '\n';
            break;
        }
    }
    out.flush();
    return all ? 0 : 3;
}

}

LINUXBEE_COMMAND(trash, "trash", "Move music files to the trash.", runTrash)
