#pragma once
#include <QDateTime>
#include <QString>
#include <QStringList>

#include <optional>

namespace linuxbee::rules {

// U-VALUES: a field's text, split at every "; ", each piece trimmed, empty
// pieces dropped.
QStringList splitValues(const QString &fieldText);

// U-NUMBER: a decimal number, or a clock time m:ss or h:mm:ss counted as seconds.
std::optional<double> readNumber(const QString &text);

// U-DATE: yyyy, yyyy-MM, yyyy-MM-dd, or an ISO 8601 date and time. A zone
// is kept as its UTC moment; a time without one is taken as UTC.
std::optional<QDateTime> readDate(const QString &text);

}
