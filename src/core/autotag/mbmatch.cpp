#include "musicbrainz.h"

#include <algorithm>
#include <vector>

#include <QtGlobal>

namespace linuxbee {

namespace {

// M-MATCH: compatibility decomposition, then lower case, then only the letters
// and digits kept. A surrogate pair is one character and is tested as one.
QString folded(const QString &text)
{
    const QString decomposed = text.normalized(QString::NormalizationForm_KD).toLower();
    QString kept;
    for (qsizetype i = 0; i < decomposed.size(); ++i) {
        const QChar current = decomposed.at(i);
        if (current.isHighSurrogate() && i + 1 < decomposed.size()
            && decomposed.at(i + 1).isLowSurrogate()) {
            const char32_t code = QChar::surrogateToUcs4(current, decomposed.at(i + 1));
            if (QChar::isLetterOrNumber(code)) {
                const char32_t single[1] = { code };
                kept += QString::fromUcs4(single, 1);
            }
            ++i;
        } else if (current.isLetterOrNumber()) {
            kept += current;
        }
    }
    return kept;
}

// The number at the start of a tag such as "05/12": -1 when there is none.
int leadingNumber(const QString &text)
{
    const QString trimmed = text.trimmed();
    qsizetype end = 0;
    while (end < trimmed.size() && trimmed.at(end) >= QLatin1Char('0')
        && trimmed.at(end) <= QLatin1Char('9'))
        ++end;
    if (end == 0)
        return -1;
    bool ok = false;
    const int number = trimmed.left(end).toInt(&ok);
    return ok ? number : -1;
}

// M-MATCH: the points for one file against one release track, 0 to 9.
int pairScore(const LocalTrack &file, const ReleaseTrack &track)
{
    int score = 0;

    // Track number and disc: 3 when both agree. A file with no Disc# is disc 1.
    int disc = leadingNumber(file.tags.first(QStringLiteral("Disc#")));
    if (disc < 0)
        disc = 1;
    if (leadingNumber(file.tags.first(QStringLiteral("Track#"))) == track.position
        && disc == track.disc)
        score += 3;

    // Title: 4 when equal after folding, 2 when one holds the other. Blank never counts.
    const QString fileTitle = folded(file.tags.first(QStringLiteral("Title")));
    const QString trackTitle = folded(track.title);
    if (!fileTitle.isEmpty() && !trackTitle.isEmpty()) {
        if (fileTitle == trackTitle)
            score += 4;
        else if (fileTitle.contains(trackTitle) || trackTitle.contains(fileTitle))
            score += 2;
    }

    // Length: only when both are known. 2 within 3 seconds, else 1 within 10 seconds.
    const int fileMs = file.tags.audio.durationMs;
    if (fileMs > 0 && track.lengthMs > 0) {
        const int apart = qAbs(fileMs - track.lengthMs);
        if (apart <= 3000)
            score += 2;
        else if (apart <= 10000)
            score += 1;
    }
    return score;
}

// M-CHANGES: a field is named when the release has a value for it and the file's
// value differs. Blank-only mode leaves any field that already has a value alone.
void offer(TagChanges &changes, const TrackTags &tags, const QString &field, const QString &value,
    bool blankFieldsOnly)
{
    if (value.isEmpty())
        return;
    const QStringList current = tags.fields.value(field);
    if (blankFieldsOnly && !current.isEmpty())
        return;
    if (current == QStringList { value })
        return;
    changes.fields.insert(field, QStringList { value });
}

}

QList<TrackMatch> matchTracks(const QList<LocalTrack> &local, const Release &release)
{
    QList<TrackMatch> result(local.size());
    if (!release.ok)
        return result;

    // Every pair that reaches 3 points counts; the best are taken first.
    struct Pair {
        int score;
        int file;
        int track;
    };
    std::vector<Pair> pairs;
    for (int f = 0; f < local.size(); ++f) {
        for (int t = 0; t < release.tracks.size(); ++t) {
            const int score = pairScore(local.at(f), release.tracks.at(t));
            if (score >= 3)
                pairs.push_back({ score, f, t });
        }
    }
    std::sort(pairs.begin(), pairs.end(), [](const Pair &a, const Pair &b) {
        if (a.score != b.score)
            return a.score > b.score;
        if (a.file != b.file)
            return a.file < b.file;
        return a.track < b.track;
    });

    std::vector<bool> fileTaken(static_cast<std::size_t>(local.size()), false);
    std::vector<bool> trackTaken(static_cast<std::size_t>(release.tracks.size()), false);
    for (const Pair &pair : pairs) {
        if (fileTaken[static_cast<std::size_t>(pair.file)]
            || trackTaken[static_cast<std::size_t>(pair.track)])
            continue;
        fileTaken[static_cast<std::size_t>(pair.file)] = true;
        trackTaken[static_cast<std::size_t>(pair.track)] = true;
        result[pair.file].releaseTrack = pair.track;
        result[pair.file].score = pair.score;
    }
    return result;
}

TagChanges releaseTagChanges(
    const TrackTags &tags, const Release &release, int releaseTrack, const AutoTagOptions &options)
{
    TagChanges changes;
    if (!release.ok || releaseTrack < 0 || releaseTrack >= release.tracks.size())
        return changes;

    const ReleaseTrack &track = release.tracks.at(releaseTrack);
    const bool blank = options.blankFieldsOnly;
    offer(changes, tags, QStringLiteral("Title"), track.title, blank);
    offer(changes, tags, QStringLiteral("Artist"), track.artist, blank);
    offer(changes, tags, QStringLiteral("Album"), release.title, blank);
    offer(changes, tags, QStringLiteral("Album Artist"), release.artist, blank);
    offer(changes, tags, QStringLiteral("Year"), release.date, blank);
    offer(changes, tags, QStringLiteral("Original Year"), release.originalDate, blank);
    offer(changes, tags, QStringLiteral("Track#"), QString::number(track.position), blank);
    offer(changes, tags, QStringLiteral("Track Count"),
        QString::number(release.tracksOnDisc(track.disc)), blank);
    offer(changes, tags, QStringLiteral("Disc#"), QString::number(track.disc), blank);
    offer(changes, tags, QStringLiteral("Disc Count"), QString::number(release.discCount), blank);
    offer(changes, tags, QStringLiteral("Publisher"), release.label, blank);
    return changes;
}

}
