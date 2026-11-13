#include "tagtools.h"

// Stub. Plan 09 replaces every body in this file.
namespace linuxbee {

QString validateSearchReplace(const SearchReplaceOptions &options)
{
    Q_UNUSED(options);
    return { };
}

TagChanges searchReplace(const TrackTags &tags, const SearchReplaceOptions &options)
{
    Q_UNUSED(tags);
    Q_UNUSED(options);
    return { };
}

QStringList defaultSmallWords()
{
    return { };
}

QString changeCase(const QString &text, const CaseOptions &options)
{
    Q_UNUSED(options);
    return text;
}

TagChanges capitalise(const TrackTags &tags, const CaseOptions &options)
{
    Q_UNUSED(tags);
    Q_UNUSED(options);
    return { };
}

TagChanges removeTags(const TrackTags &tags, const RemoveOptions &options)
{
    Q_UNUSED(tags);
    Q_UNUSED(options);
    return { };
}

QList<TagChanges> renumberTracks(const QList<TrackTags> &tracks, const RenumberOptions &options)
{
    Q_UNUSED(tracks);
    Q_UNUSED(options);
    return { };
}

QString validateInferPattern(const QString &pattern)
{
    Q_UNUSED(pattern);
    return { };
}

InferResult inferFromPath(const QString &path, const QString &pattern)
{
    Q_UNUSED(path);
    Q_UNUSED(pattern);
    return { };
}

TagChanges inferTags(
    const TrackTags &tags, const QString &path, const QString &pattern, bool blankFieldsOnly)
{
    Q_UNUSED(tags);
    Q_UNUSED(path);
    Q_UNUSED(pattern);
    Q_UNUSED(blankFieldsOnly);
    return { };
}

TagChanges copyTag(const TrackTags &tags, const QString &from, const QString &to)
{
    Q_UNUSED(tags);
    Q_UNUSED(from);
    Q_UNUSED(to);
    return { };
}

TagChanges swapTags(const TrackTags &tags, const QString &a, const QString &b)
{
    Q_UNUSED(tags);
    Q_UNUSED(a);
    Q_UNUSED(b);
    return { };
}

}
