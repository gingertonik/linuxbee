#include "autoplaylist.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <cmath>
#include <climits>

namespace linuxbee {

namespace {

struct LimitUnitName {
    AutoPlaylist::LimitUnit unit;
    const char *id;
};

struct SelectedByName {
    AutoPlaylist::SelectedBy selectedBy;
    const char *id;
};

// A-NAMES, in enum order.
const LimitUnitName kLimitUnits[] = {
    { AutoPlaylist::LimitUnit::Items, "items" },
    { AutoPlaylist::LimitUnit::Minutes, "minutes" },
    { AutoPlaylist::LimitUnit::Hours, "hours" },
    { AutoPlaylist::LimitUnit::Megabytes, "megabytes" },
    { AutoPlaylist::LimitUnit::Gigabytes, "gigabytes" },
    { AutoPlaylist::LimitUnit::Albums, "albums" },
};

// A-NAMES, in enum order.
const SelectedByName kSelectedBy[] = {
    { AutoPlaylist::SelectedBy::Random, "random" },
    { AutoPlaylist::SelectedBy::HighestRating, "highest-rating" },
    { AutoPlaylist::SelectedBy::LowestRating, "lowest-rating" },
    { AutoPlaylist::SelectedBy::MostRecentlyPlayed, "most-recently-played" },
    { AutoPlaylist::SelectedBy::LeastRecentlyPlayed, "least-recently-played" },
    { AutoPlaylist::SelectedBy::MostOftenPlayed, "most-often-played" },
    { AutoPlaylist::SelectedBy::LeastOftenPlayed, "least-often-played" },
    { AutoPlaylist::SelectedBy::MostRecentlyAdded, "most-recently-added" },
    { AutoPlaylist::SelectedBy::LeastRecentlyAdded, "least-recently-added" },
};

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

bool limitUnitFromId(const QString &id, AutoPlaylist::LimitUnit *unit)
{
    for (const LimitUnitName &entry : kLimitUnits) {
        if (id == QLatin1String(entry.id)) {
            *unit = entry.unit;
            return true;
        }
    }
    return false;
}

bool selectedByFromId(const QString &id, AutoPlaylist::SelectedBy *selectedBy)
{
    for (const SelectedByName &entry : kSelectedBy) {
        if (id == QLatin1String(entry.id)) {
            *selectedBy = entry.selectedBy;
            return true;
        }
    }
    return false;
}

}

QString limitUnitId(AutoPlaylist::LimitUnit unit)
{
    for (const LimitUnitName &entry : kLimitUnits) {
        if (entry.unit == unit)
            return QLatin1String(entry.id);
    }
    return { };
}

QString selectedById(AutoPlaylist::SelectedBy selectedBy)
{
    for (const SelectedByName &entry : kSelectedBy) {
        if (entry.selectedBy == selectedBy)
            return QLatin1String(entry.id);
    }
    return { };
}

QString autoPlaylistToJson(const AutoPlaylist &playlist)
{
    QString out = QStringLiteral("{\"version\":1,\"rules\":");
    out.append(rulesToJson(playlist.rules));
    out.append(QStringLiteral(",\"limit\":"));
    if (playlist.limitEnabled) {
        out.append(QStringLiteral("{\"amount\":%1,\"unit\":\"%2\"}")
                .arg(playlist.limitAmount)
                .arg(limitUnitId(playlist.limitUnit)));
    } else {
        out.append(QStringLiteral("null"));
    }
    out.append(QStringLiteral(",\"selectedBy\":\"%1\",\"filterDuplicates\":%2}")
            .arg(selectedById(playlist.selectedBy))
            .arg(playlist.filterDuplicates ? QStringLiteral("true") : QStringLiteral("false")));
    return out;
}

bool autoPlaylistFromJson(const QString &json, AutoPlaylist *playlist, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return fail(error, QStringLiteral("not JSON: %1").arg(parseError.errorString()));
    if (!document.isObject())
        return fail(error, QStringLiteral("an auto-playlist is a JSON object"));

    // Unknown keys are refused, so a typo is not silently ignored.
    const QJsonObject object = document.object();
    const QStringList known = { QStringLiteral("version"), QStringLiteral("rules"),
        QStringLiteral("limit"), QStringLiteral("selectedBy"), QStringLiteral("filterDuplicates") };
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!known.contains(it.key()))
            return fail(error, QStringLiteral("unknown key \"%1\"").arg(it.key()));
    }

    // Built apart, so *playlist changes only when every part has read.
    AutoPlaylist result;

    const QJsonValue version = object.value(QStringLiteral("version"));
    if (!version.isUndefined() && !(version.isDouble() && version.toDouble() == 1))
        return fail(error, QStringLiteral("version must be 1"));

    const QJsonValue rules = object.value(QStringLiteral("rules"));
    if (!rules.isUndefined()) {
        if (!rules.isObject())
            return fail(error, QStringLiteral("rules is not a rule set"));
        const QString rulesText
            = QString::fromUtf8(QJsonDocument(rules.toObject()).toJson(QJsonDocument::Compact));
        QString rulesError;
        if (!rulesFromJson(rulesText, &result.rules, &rulesError))
            return fail(error, rulesError);
    }

    // A-JSON: a missing or null limit is off, and then the amount and unit
    // stay at their defaults.
    const QJsonValue limit = object.value(QStringLiteral("limit"));
    if (!limit.isUndefined() && !limit.isNull()) {
        if (!limit.isObject())
            return fail(error, QStringLiteral("limit is not an object"));
        const QJsonObject limitObject = limit.toObject();
        const QJsonValue amount = limitObject.value(QStringLiteral("amount"));
        if (!amount.isDouble())
            return fail(error, QStringLiteral("limit amount is not a number"));
        const double value = amount.toDouble();
        if (value != std::floor(value) || value < 1 || value > INT_MAX)
            return fail(error, QStringLiteral("limit amount must be a whole number of 1 or more"));
        const QJsonValue unit = limitObject.value(QStringLiteral("unit"));
        AutoPlaylist::LimitUnit parsedUnit = AutoPlaylist::LimitUnit::Items;
        if (!unit.isString() || !limitUnitFromId(unit.toString(), &parsedUnit))
            return fail(error, QStringLiteral("limit unit is not known"));
        result.limitEnabled = true;
        result.limitAmount = int(value);
        result.limitUnit = parsedUnit;
    }

    const QJsonValue selected = object.value(QStringLiteral("selectedBy"));
    if (!selected.isUndefined()) {
        if (!selected.isString() || !selectedByFromId(selected.toString(), &result.selectedBy))
            return fail(error, QStringLiteral("selectedBy is not a known selection"));
    }

    const QJsonValue duplicates = object.value(QStringLiteral("filterDuplicates"));
    if (!duplicates.isUndefined()) {
        if (!duplicates.isBool())
            return fail(error, QStringLiteral("filterDuplicates must be true or false"));
        result.filterDuplicates = duplicates.toBool();
    }

    *playlist = result;
    return true;
}

}
