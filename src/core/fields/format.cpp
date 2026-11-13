#include "format.h"

#include <QChar>

namespace linuxbee::fieldformat {

QString duration(qint64 milliseconds)
{
    const qint64 ms = milliseconds < 0 ? 0 : milliseconds;
    const qint64 seconds = (ms + 500) / 1000;
    const qint64 hours = seconds / 3600;
    const qint64 minutes = (seconds % 3600) / 60;
    const qint64 secs = seconds % 60;

    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(secs, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(minutes).arg(secs, 2, 10, QLatin1Char('0'));
}

}
