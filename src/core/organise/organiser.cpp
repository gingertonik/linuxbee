#include "organiser.h"

// Stub. Plan 07 replaces every body in this file.
namespace linuxbee {

QList<PlannedMove> planOrganise(const QStringList &files, const OrganiseOptions &options)
{
    Q_UNUSED(files);
    Q_UNUSED(options);
    return { };
}

OrganiseReport executeOrganise(const QList<PlannedMove> &plan, const OrganiseOptions &options,
    const OrganiseProgress &progress)
{
    Q_UNUSED(plan);
    Q_UNUSED(options);
    Q_UNUSED(progress);
    return { };
}

}
