#pragma once
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include "tags/tagreader.h"
#include "tags/tagwriter.h"

// Tools that work out tag changes for many tracks at once: search and
// replace, capitalise, remove tags, renumber, infer from the file name, copy
// and swap. Contract: docs/spec/tagging-tools.md.
//
// Every function here is pure: it takes the tags a track has and returns the
// changes to make, naming only what would really change. Nothing reads or
// writes a file; the caller shows the changes, then hands them to writeTags.
namespace linuxbee {

struct SearchReplaceOptions {
    QString find;
    QString replace;
    QStringList fields; // empty: every catalogue field except Rating
    bool regex = false;
    bool matchCase = false;
    bool wholeWord = false;
};
// G-REPLACE. Empty when the options can be used, otherwise what is wrong.
QString validateSearchReplace(const SearchReplaceOptions &options);
TagChanges searchReplace(const TrackTags &tags, const SearchReplaceOptions &options);

enum class CaseStyle { EachWord, TitleCase, Sentence, Upper, Lower };
struct CaseOptions {
    CaseStyle style = CaseStyle::EachWord;
    QStringList fields; // empty: Title, Artist, Album, Album Artist
    QStringList smallWords; // TitleCase only; empty: defaultSmallWords()
};
QStringList defaultSmallWords();
// G-CASE
QString changeCase(const QString &text, const CaseOptions &options);
TagChanges capitalise(const TrackTags &tags, const CaseOptions &options);

// G-REMOVE. `fields` names catalogue fields and other keys; with allTags
// every text tag goes. removePictures empties the picture list.
struct RemoveOptions {
    QStringList fields;
    bool allTags = false;
    bool removePictures = false;
};
TagChanges removeTags(const TrackTags &tags, const RemoveOptions &options);

struct RenumberOptions {
    int start = 1;
    int digits = 0; // pad with zeros to this many digits; 0: no padding
    bool writeTotal = false; // also set Track Count to the number of tracks
};
// G-RENUMBER: one TagChanges per track, in the order given.
QList<TagChanges> renumberTracks(const QList<TrackTags> &tracks, const RenumberOptions &options);

// G-INFER
struct InferResult {
    bool matched = false;
    QMap<QString, QString> values; // field name -> text found
};
QString validateInferPattern(const QString &pattern);
InferResult inferFromPath(const QString &path, const QString &pattern);
TagChanges inferTags(
    const TrackTags &tags, const QString &path, const QString &pattern, bool blankFieldsOnly);

// G-COPY
TagChanges copyTag(const TrackTags &tags, const QString &from, const QString &to);
TagChanges swapTags(const TrackTags &tags, const QString &a, const QString &b);

}
