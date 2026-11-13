#include "musicbrainz.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMap>
#include <QStringList>

#include <utility>

namespace linuxbee {

namespace {

// Reads an answer. A failure carries the service's own error text when the
// answer has one, else a short description of what is wrong with it.
bool readObject(const QByteArray &json, QJsonObject *object, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        *error = QStringLiteral("the answer is not JSON: ") + parseError.errorString();
        return false;
    }
    if (!document.isObject()) {
        *error = QStringLiteral("the answer is not a JSON object");
        return false;
    }
    *object = document.object();
    if (object->contains(QStringLiteral("error"))) {
        const QString message = object->value(QStringLiteral("error")).toString();
        *error = message.isEmpty() ? QStringLiteral("the service returned an error") : message;
        return false;
    }
    return true;
}

// M-SEARCH, M-RELEASE: the text of an artist credit. Each part gives its name
// (else its artist's name) and its joinphrase; the whole is trimmed.
QString creditText(const QJsonValue &credit)
{
    QString text;
    for (const QJsonValue &value : credit.toArray()) {
        const QJsonObject part = value.toObject();
        QString name = part.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
            name = part.value(QStringLiteral("artist"))
                       .toObject()
                       .value(QStringLiteral("name"))
                       .toString();
        text += name + part.value(QStringLiteral("joinphrase")).toString();
    }
    return text.trimmed();
}

// M-SEARCH: the score is a number, or a number written as text.
int scoreOf(const QJsonValue &value)
{
    if (value.isDouble())
        return value.toInt();
    if (value.isString())
        return value.toString().toInt();
    return 0;
}

// M-SEARCH: formats in order of first appearance; a count and a multiplication
// sign before a format that occurs more than once; no format counts as "?".
QString formatsOf(const QJsonArray &media)
{
    QStringList order;
    QMap<QString, int> counts;
    for (const QJsonValue &value : media) {
        QString format = value.toObject().value(QStringLiteral("format")).toString();
        if (format.isEmpty())
            format = QStringLiteral("?");
        if (!counts.contains(format))
            order.append(format);
        counts[format] += 1;
    }
    QStringList parts;
    for (const QString &format : std::as_const(order)) {
        const int count = counts.value(format);
        parts.append(count > 1 ? QString::number(count) + QChar(0x00D7) + format : format);
    }
    return parts.join(QStringLiteral(" + "));
}

// The first label-info entry gives the label and its catalogue number.
void firstLabel(const QJsonObject &release, QString *label, QString *catalogNumber)
{
    const QJsonArray info = release.value(QStringLiteral("label-info")).toArray();
    if (info.isEmpty())
        return;
    const QJsonObject first = info.at(0).toObject();
    *label
        = first.value(QStringLiteral("label")).toObject().value(QStringLiteral("name")).toString();
    *catalogNumber = first.value(QStringLiteral("catalog-number")).toString();
}

// M-RELEASE: a track's length is its own, else its recording's, else 0.
int lengthOf(const QJsonObject &track, const QJsonObject &recording)
{
    const QJsonValue own = track.value(QStringLiteral("length"));
    if (own.isDouble())
        return own.toInt();
    const QJsonValue fromRecording = recording.value(QStringLiteral("length"));
    if (fromRecording.isDouble())
        return fromRecording.toInt();
    return 0;
}

// M-RELEASE: discs count from 1 and positions count within a disc from 1,
// whatever numbers the answer carries.
QList<ReleaseTrack> tracksOf(const QJsonArray &media, const QString &releaseArtist)
{
    QList<ReleaseTrack> tracks;
    int disc = 0;
    for (const QJsonValue &mediumValue : media) {
        ++disc;
        int position = 0;
        const QJsonArray entries = mediumValue.toObject().value(QStringLiteral("tracks")).toArray();
        for (const QJsonValue &entry : entries) {
            if (!entry.isObject())
                continue;
            const QJsonObject track = entry.toObject();
            const QJsonObject recording = track.value(QStringLiteral("recording")).toObject();

            ReleaseTrack item;
            item.disc = disc;
            item.position = ++position;
            item.title = track.value(QStringLiteral("title")).toString();
            if (item.title.isEmpty())
                item.title = recording.value(QStringLiteral("title")).toString();
            item.artist = creditText(track.value(QStringLiteral("artist-credit")));
            if (item.artist.isEmpty())
                item.artist = creditText(recording.value(QStringLiteral("artist-credit")));
            if (item.artist.isEmpty())
                item.artist = releaseArtist;
            item.lengthMs = lengthOf(track, recording);
            item.recordingId = recording.value(QStringLiteral("id")).toString();
            tracks.append(item);
        }
    }
    return tracks;
}

}

int Release::tracksOnDisc(int disc) const
{
    int count = 0;
    for (const ReleaseTrack &track : tracks) {
        if (track.disc == disc)
            ++count;
    }
    return count;
}

ReleaseSearch parseReleaseSearch(const QByteArray &json)
{
    ReleaseSearch result;
    QJsonObject root;
    if (!readObject(json, &root, &result.error))
        return result;
    const QJsonValue list = root.value(QStringLiteral("releases"));
    if (!list.isArray()) {
        result.error = QStringLiteral("the answer has no releases list");
        return result;
    }

    for (const QJsonValue &value : list.toArray()) {
        const QJsonObject entry = value.toObject();
        ReleaseCandidate candidate;
        candidate.id = entry.value(QStringLiteral("id")).toString();
        // An entry whose id would not pass the M-QUERY check is dropped.
        if (!releaseLookupUrl(candidate.id).isValid())
            continue;
        const QJsonArray media = entry.value(QStringLiteral("media")).toArray();
        candidate.title = entry.value(QStringLiteral("title")).toString();
        candidate.artist = creditText(entry.value(QStringLiteral("artist-credit")));
        candidate.date = entry.value(QStringLiteral("date")).toString();
        candidate.country = entry.value(QStringLiteral("country")).toString();
        candidate.status = entry.value(QStringLiteral("status")).toString();
        candidate.trackCount = entry.value(QStringLiteral("track-count")).toInt();
        candidate.score = scoreOf(entry.value(QStringLiteral("score")));
        firstLabel(entry, &candidate.label, &candidate.catalogNumber);
        candidate.discCount = static_cast<int>(media.size());
        candidate.format = formatsOf(media);
        result.releases.append(candidate);
    }
    result.ok = true;
    return result;
}

Release parseRelease(const QByteArray &json)
{
    Release result;
    QJsonObject root;
    if (!readObject(json, &root, &result.error))
        return result;

    const QString id = root.value(QStringLiteral("id")).toString();
    if (!releaseLookupUrl(id).isValid()) {
        result.error = QStringLiteral("the answer has no valid release id");
        return result;
    }
    const QJsonValue media = root.value(QStringLiteral("media"));
    if (!media.isArray()) {
        result.error = QStringLiteral("the answer has no media list");
        return result;
    }

    result.ok = true;
    result.id = id;
    result.title = root.value(QStringLiteral("title")).toString();
    result.artist = creditText(root.value(QStringLiteral("artist-credit")));
    result.date = root.value(QStringLiteral("date")).toString();
    result.country = root.value(QStringLiteral("country")).toString();
    firstLabel(root, &result.label, &result.catalogNumber);
    result.originalDate = root.value(QStringLiteral("release-group"))
                              .toObject()
                              .value(QStringLiteral("first-release-date"))
                              .toString();
    result.hasFrontCover = root.value(QStringLiteral("cover-art-archive"))
                               .toObject()
                               .value(QStringLiteral("front"))
                               .toBool();
    result.discCount = static_cast<int>(media.toArray().size());
    result.tracks = tracksOf(media.toArray(), result.artist);
    return result;
}

}
