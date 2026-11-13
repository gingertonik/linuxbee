#include "version.h"

namespace linuxbee {
QString versionString()
{
    return QStringLiteral(LINUXBEE_VERSION);
}
}
