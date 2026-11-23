#include "library/mounts.h"

// TO BE BUILT (fix-m1b): the red commit carries this stub so that the locked test builds; it reads
// nothing. The builder replaces it with the readers moved out of scan.cpp (TASK.md).
namespace linuxbee::mounts {

QStringList mountFieldOf(const QString &, int)
{
    return { };
}

QStringList tableMountPoints()
{
    return { };
}

QSet<QString> mountedNow()
{
    return { };
}

}
