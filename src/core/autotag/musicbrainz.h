#pragma once
#include <QByteArray>
#include <QList>
#include <QString>
#include <QUrl>

#include "tags/tagreader.h"
#include "tags/tagwriter.h"

// Auto-tagging an album from MusicBrainz: the questions to ask, how to read
// the answers, which file is which track, and the tag changes that follow.
// Contract: docs/spec/auto-tag.md. Nothing here touches the network or a
// file: the caller fetches the URLs and saves the changes.
namespace linuxbee {

// M-AGENT: the User-Agent every request must carry.
QString musicBrainzUserAgent();

// M-QUERY
QUrl releaseSearchUrl(const QString &artist, const QString &album, int limit = 10);
QUrl releaseLookupUrl(const QString &releaseId);
// size: 250, 500 or 1200 pixels, or 0 for the original.
QUrl frontCoverUrl(const QString &releaseId, int size = 500);

struct ReleaseCandidate {
    QString id;
    QString title;
    QString artist;
    QString date; // as given: "1997", "1997-06", "1997-06-16" or empty
    QString country;
    QString label;
    QString catalogNumber;
    QString format; // "CD", "2×CD", "CD + DVD"
    QString status;
    int trackCount = 0;
    int discCount = 0;
    int score = 0; // 0 to 100, the service's own
};

struct ReleaseSearch {
    bool ok = false;
    QString error;
    QList<ReleaseCandidate> releases; // in the order given
};
// M-SEARCH
ReleaseSearch parseReleaseSearch(const QByteArray &json);

struct ReleaseTrack {
    int disc = 1;
    int position = 0; // within its disc, from 1
    QString title;
    QString artist;
    int lengthMs = 0; // 0: not known
    QString recordingId;
};

struct Release {
    bool ok = false;
    QString error;
    QString id;
    QString title;
    QString artist;
    QString date;
    QString originalDate; // the release group's first release
    QString country;
    QString label;
    QString catalogNumber;
    int discCount = 0;
    bool hasFrontCover = false;
    QList<ReleaseTrack> tracks; // disc by disc, in order

    int tracksOnDisc(int disc) const;
};
// M-RELEASE
Release parseRelease(const QByteArray &json);

struct LocalTrack {
    TrackTags tags;
    QString path;
};
struct TrackMatch {
    int releaseTrack = -1; // index into Release::tracks; -1: no match
    int score = 0; // M-MATCH, 0 to 9
};
// M-MATCH: one entry per local track, in the order given.
QList<TrackMatch> matchTracks(const QList<LocalTrack> &local, const Release &release);

struct AutoTagOptions {
    bool blankFieldsOnly = false;
};
// M-CHANGES: what to write to a file matched to release.tracks[releaseTrack].
TagChanges releaseTagChanges(const TrackTags &tags, const Release &release, int releaseTrack,
    const AutoTagOptions &options = { });

}
