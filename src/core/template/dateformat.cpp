#include "dateformat.h"

#include <QLatin1String>
#include <QDate>
#include <QDateTime>
#include <QLocale>
#include <QRegularExpression>
#include <QString>
#include <QTime>

#include <array>
#include <optional>

namespace linuxbee::tmpl {
namespace {

// T-DATE: an ISO 8601 date, an ISO 8601 date and time, or a four-digit year
// meaning 1 January. Anything else is not a date.
std::optional<QDateTime> readDate(const QString &value)
{
    // Built once; QRegularExpression is reentrant when used read-only.
    static const QRegularExpression kFourDigitYear(QStringLiteral("^\\s*\\d{4}\\s*$"));
    if (kFourDigitYear.match(value).hasMatch()) {
        const QDate date(value.trimmed().toInt(), 1, 1);
        if (!date.isValid())
            return std::nullopt;
        return QDateTime(date, QTime(0, 0));
    }
    const QDateTime parsed = QDateTime::fromString(value, Qt::ISODate);
    if (!parsed.isValid())
        return std::nullopt;
    return parsed;
}

enum Which {
    YearFour,
    YearTwo,
    MonthLong,
    MonthShort,
    MonthPadded,
    MonthPlain,
    DayOfWeekLong,
    DayOfWeekShort,
    DayPadded,
    DayPlain,
    Hour24Padded,
    Hour24Plain,
    Hour12Padded,
    Hour12Plain,
    MinutePadded,
    MinutePlain,
    SecondPadded,
    SecondPlain,
};

struct DateToken {
    QLatin1StringView token;
    Which which;
};

// The spec's table, in its own order, which is longest token first inside
// every family: yyyy before yy, MMMM before MMM before MM before M, and so
// on. Scanning takes the first token that matches at the current position.
constexpr std::array<DateToken, 18> kTokens { {
    { QLatin1StringView("yyyy"), YearFour },
    { QLatin1StringView("yy"), YearTwo },
    { QLatin1StringView("MMMM"), MonthLong },
    { QLatin1StringView("MMM"), MonthShort },
    { QLatin1StringView("MM"), MonthPadded },
    { QLatin1StringView("M"), MonthPlain },
    { QLatin1StringView("dddd"), DayOfWeekLong },
    { QLatin1StringView("ddd"), DayOfWeekShort },
    { QLatin1StringView("dd"), DayPadded },
    { QLatin1StringView("d"), DayPlain },
    { QLatin1StringView("HH"), Hour24Padded },
    { QLatin1StringView("H"), Hour24Plain },
    { QLatin1StringView("hh"), Hour12Padded },
    { QLatin1StringView("h"), Hour12Plain },
    { QLatin1StringView("mm"), MinutePadded },
    { QLatin1StringView("m"), MinutePlain },
    { QLatin1StringView("ss"), SecondPadded },
    { QLatin1StringView("s"), SecondPlain },
} };

QString padded(int value, qsizetype width)
{
    return QString::number(value).rightJustified(width, QLatin1Char('0'));
}

// Does the text begin with this ASCII token? Written by hand because a Latin-1
// string has no QStringView in Qt 6, and QChar(char) is an ASCII cast the build
// forbids.
bool startsWithToken(QStringView text, QLatin1StringView token)
{
    if (text.size() < token.size())
        return false;
    for (qsizetype index = 0; index < token.size(); ++index) {
        if (text.at(index) != QChar(char16_t(uchar(token[index].toLatin1()))))
            return false;
    }
    return true;
}

// The 12-hour clock. The spec has no AM/PM token, so `h` is 1 to 12 from the
// 24-hour hour, which is why the format is not handed to QDateTime.
int hour12(int hour)
{
    const int wrapped = hour % 12;
    return wrapped == 0 ? 12 : wrapped;
}

QString tokenText(Which which, const QDate &date, const QTime &time, const QLocale &locale)
{
    switch (which) {
    case YearFour:
        return padded(date.year(), 4);
    case YearTwo:
        return padded(date.year() % 100, 2);
    case MonthLong:
        return locale.standaloneMonthName(date.month(), QLocale::LongFormat);
    case MonthShort:
        return locale.standaloneMonthName(date.month(), QLocale::ShortFormat);
    case MonthPadded:
        return padded(date.month(), 2);
    case MonthPlain:
        return QString::number(date.month());
    case DayOfWeekLong:
        return locale.standaloneDayName(date.dayOfWeek(), QLocale::LongFormat);
    case DayOfWeekShort:
        return locale.standaloneDayName(date.dayOfWeek(), QLocale::ShortFormat);
    case DayPadded:
        return padded(date.day(), 2);
    case DayPlain:
        return QString::number(date.day());
    case Hour24Padded:
        return padded(time.hour(), 2);
    case Hour24Plain:
        return QString::number(time.hour());
    case Hour12Padded:
        return padded(hour12(time.hour()), 2);
    case Hour12Plain:
        return QString::number(hour12(time.hour()));
    case MinutePadded:
        return padded(time.minute(), 2);
    case MinutePlain:
        return QString::number(time.minute());
    case SecondPadded:
        return padded(time.second(), 2);
    case SecondPlain:
        return QString::number(time.second());
    }
    return { };
}

} // namespace

QString formatDate(const QString &value, const QString &format)
{
    const std::optional<QDateTime> moment = readDate(value);
    if (!moment.has_value())
        return { }; // T-DATE: x is not a date
    const QDate date = moment->date();
    const QTime time = moment->time();
    const QLocale locale = QLocale::c(); // T-DATE: names never follow the system language
    QString out;
    qsizetype position = 0;
    while (position < format.size()) {
        const QStringView rest = QStringView(format).mid(position);
        bool replaced = false;
        for (const DateToken &entry : kTokens) {
            if (!startsWithToken(rest, entry.token))
                continue;
            out += tokenText(entry.which, date, time, locale);
            position += entry.token.size();
            replaced = true;
            break;
        }
        if (!replaced) {
            // Any other character is copied (T-DATE).
            out += format.at(position);
            ++position;
        }
    }
    return out;
}

} // namespace linuxbee::tmpl
