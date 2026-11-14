#include "common.h"
#include "tagtools.h"

namespace linuxbee {

namespace {

using tagtools::canonicalField;
using tagtools::setIfChanged;

}

// G-REMOVE. `fields` names catalogue fields (case ignored) and other keys
// (matched exactly). allTags removes every field and other key that has a value.
TagChanges removeTags(const TrackTags &tags, const RemoveOptions &options)
{
    TagChanges changes;
    if (options.allTags) {
        for (auto it = tags.fields.cbegin(); it != tags.fields.cend(); ++it) {
            if (!it.value().isEmpty())
                changes.fields.insert(it.key(), QStringList());
        }
        for (auto it = tags.other.cbegin(); it != tags.other.cend(); ++it) {
            if (!it.value().isEmpty())
                changes.other.insert(it.key(), QStringList());
        }
    }
    for (const QString &name : options.fields) {
        const QString field = canonicalField(name);
        if (!field.isEmpty()) {
            if (!tags.fields.value(field).isEmpty())
                changes.fields.insert(field, QStringList());
        } else if (!tags.other.value(name).isEmpty()) {
            changes.other.insert(name, QStringList());
        }
    }
    if (options.removePictures && !tags.pictures.isEmpty())
        changes.pictures = QList<Picture>();
    return changes;
}

// G-RENUMBER: one result per track, in the order given. Track Count is
// written as the number of tracks, without padding. A negative start or
// digits, or more than nine digits, gives no results at all.
QList<TagChanges> renumberTracks(const QList<TrackTags> &tracks, const RenumberOptions &options)
{
    QList<TagChanges> results;
    if (options.start < 0 || options.digits < 0 || options.digits > 9)
        return results;

    const QString total = QString::number(int(tracks.size()));
    for (qsizetype i = 0; i < tracks.size(); ++i) {
        const QString number = QString::number(qint64(options.start) + qint64(i))
                                   .rightJustified(options.digits, QLatin1Char('0'));
        TagChanges changes;
        setIfChanged(&changes, tracks.at(i), QStringLiteral("Track#"), { number });
        if (options.writeTotal)
            setIfChanged(&changes, tracks.at(i), QStringLiteral("Track Count"), { total });
        results.append(changes);
    }
    return results;
}

// G-COPY: `to` takes the values of `from`; a `from` with none clears `to`.
TagChanges copyTag(const TrackTags &tags, const QString &from, const QString &to)
{
    TagChanges changes;
    const QString source = canonicalField(from);
    const QString target = canonicalField(to);
    if (source.isEmpty() || target.isEmpty() || source == target)
        return changes;
    setIfChanged(&changes, tags, target, tags.fields.value(source));
    return changes;
}

// G-COPY: exchanges the values of two fields.
TagChanges swapTags(const TrackTags &tags, const QString &a, const QString &b)
{
    TagChanges changes;
    const QString first = canonicalField(a);
    const QString second = canonicalField(b);
    if (first.isEmpty() || second.isEmpty() || first == second)
        return changes;
    setIfChanged(&changes, tags, first, tags.fields.value(second));
    setIfChanged(&changes, tags, second, tags.fields.value(first));
    return changes;
}

}
