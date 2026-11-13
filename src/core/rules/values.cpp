#include "values.h"

#include <QDate>
#include <QRegularExpression>
#include <QTime>
#include <QTimeZone>

namespace linuxbee::rules {

QStringList splitValues(const QString &fieldText)
{
    QStringList values;
    for (const QString &piece : fieldText.split(QStringLiteral("; "))) {
        const QString value = piece.trimmed();
        if (!value.isEmpty())
            values.append(value);
    }
    return values;
}

std::optional<double> readNumber(const QString &text)
{
    static const QRegularExpression decimal(QStringLiteral("^-?\\d+(\\.\\d+)?$"));
    static const QRegularExpression minutesSeconds(QStringLiteral("^(\\d+):(\\d{2})$"));
    static const QRegularExpression hoursMinutesSeconds(
        QStringLiteral("^(\\d+):(\\d{2}):(\\d{2})$"));

    const QString trimmed = text.trimmed();
    if (decimal.match(trimmed).hasMatch())
        return trimmed.toDouble();

    const QRegularExpressionMatch ms = minutesSeconds.match(trimmed);
    if (ms.hasMatch()) {
        const int seconds = ms.captured(2).toInt();
        if (seconds >= 60)
            return std::nullopt;
        return ms.captured(1).toDouble() * 60 + seconds;
    }

    const QRegularExpressionMatch hms = hoursMinutesSeconds.match(trimmed);
    if (hms.hasMatch()) {
        const int minutes = hms.captured(2).toInt();
        const int seconds = hms.captured(3).toInt();
        if (minutes >= 60 || seconds >= 60)
            return std::nullopt;
        return hms.captured(1).toDouble() * 3600 + minutes * 60 + seconds;
    }
    return std::nullopt;
}

std::optional<QDateTime> readDate(const QString &text)
{
    static const QRegularExpression year(QStringLiteral("^(\\d{4})$"));
    static const QRegularExpression month(QStringLiteral("^(\\d{4})-(\\d{2})$"));
    static const QRegularExpression day(QStringLiteral("^(\\d{4})-(\\d{2})-(\\d{2})$"));
    // A zone at the end of a date and time: Z, +hh, +hhmm, +hh:mm.
    static const QRegularExpression zone(QStringLiteral("(Z|[+-]\\d{2}(:?\\d{2})?)$"));

    const QString trimmed = text.trimmed();

    const QRegularExpressionMatch y = year.match(trimmed);
    if (y.hasMatch()) {
        const QDate date(y.captured(1).toInt(), 1, 1);
        if (!date.isValid())
            return std::nullopt;
        return QDateTime(date, QTime(0, 0), QTimeZone::UTC);
    }
    const QRegularExpressionMatch m = month.match(trimmed);
    if (m.hasMatch()) {
        const QDate date(m.captured(1).toInt(), m.captured(2).toInt(), 1);
        if (!date.isValid())
            return std::nullopt;
        return QDateTime(date, QTime(0, 0), QTimeZone::UTC);
    }
    const QRegularExpressionMatch d = day.match(trimmed);
    if (d.hasMatch()) {
        const QDate date(d.captured(1).toInt(), d.captured(2).toInt(), d.captured(3).toInt());
        if (!date.isValid())
            return std::nullopt;
        return QDateTime(date, QTime(0, 0), QTimeZone::UTC);
    }

    if (!trimmed.contains(QLatin1Char('T')))
        return std::nullopt;
    const QDateTime parsed = QDateTime::fromString(trimmed, Qt::ISODate);
    if (!parsed.isValid())
        return std::nullopt;
    if (zone.match(trimmed).hasMatch())
        return parsed.toUTC();
    // No zone: Qt reads it as local time. The same wall-clock reading is UTC.
    return QDateTime(parsed.date(), parsed.time(), QTimeZone::UTC);
}

}
