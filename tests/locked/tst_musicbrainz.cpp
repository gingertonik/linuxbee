// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Every expected value is a rule in docs/spec/auto-tag.md (M-x) applied by
// hand to the two saved answers in tests/data/musicbrainz/ (see the README
// there). MB is the MusicBrainz API documentation. No test uses the network.
#include <QUrlQuery>
#include <QtTest>

#include "autotag/musicbrainz.h"
#include "version.h"

using linuxbee::LocalTrack;
using linuxbee::Release;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

QByteArray fixture(const char *name)
{
    QFile f(QStringLiteral(LINUXBEE_TEST_DATA "/musicbrainz/") + QLatin1String(name));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

const QString kId = QStringLiteral("e8c3ff98-75f0-383f-a7d1-35db67661d89");

LocalTrack local(const char16_t *title, const char16_t *track = nullptr, const char16_t *disc = nullptr, int seconds = 0)
{
    LocalTrack t;
    if (title)
        t.tags.fields.insert(s(u"Title"), { s(title) });
    if (track)
        t.tags.fields.insert(s(u"Track#"), { s(track) });
    if (disc)
        t.tags.fields.insert(s(u"Disc#"), { s(disc) });
    t.tags.audio.durationMs = seconds * 1000;
    return t;
}

using Map = QMap<QString, QStringList>;

}

class MusicBrainzTest : public QObject {
    Q_OBJECT
private slots:
    void urls();
    void search();
    void release();
    void errors_data();
    void errors();
    void match();
    void changes();
};

void MusicBrainzTest::urls()
{
    // M-AGENT (MB: a meaningful User-Agent with contact information is required)
    const QString agent = linuxbee::musicBrainzUserAgent();
    const QRegularExpression shape(QStringLiteral("^linuxBee/(\\S+) \\( https://\\S+ \\)$"));
    const auto m = shape.match(agent);
    QVERIFY2(m.hasMatch(), qPrintable(agent));
    QCOMPARE(m.captured(1), linuxbee::versionString());

    // M-QUERY
    QUrl url = linuxbee::releaseSearchUrl(s(u"Radiohead"), s(u"OK Computer"), 3);
    QCOMPARE(url.scheme(), s(u"https"));
    QCOMPARE(url.host(), s(u"musicbrainz.org"));
    QCOMPARE(url.path(), s(u"/ws/2/release/"));
    QUrlQuery q(url);
    QCOMPARE(q.queryItemValue(s(u"query"), QUrl::FullyDecoded), s(u"artist:\"Radiohead\" AND release:\"OK Computer\""));
    QCOMPARE(q.queryItemValue(s(u"fmt")), s(u"json"));
    QCOMPARE(q.queryItemValue(s(u"limit")), s(u"3"));
    QCOMPARE(q.queryItems().size(), 3);
    // What goes over the wire is plain ASCII, with nothing left that could split the query.
    const QByteArray wire = url.toEncoded();
    QVERIFY(!wire.contains(' ') && !wire.contains('"'));
    for (const char c : wire)
        QVERIFY(static_cast<unsigned char>(c) < 0x80);
    // Text that would otherwise be syntax: quotes and backslashes are escaped inside the phrase,
    // and & # + = stay part of the value.
    url = linuxbee::releaseSearchUrl(s(u"AC/DC & \"Friends\""), s(u"Back\\Black #1 +=?"), 10);
    q = QUrlQuery(url);
    QCOMPARE(q.queryItemValue(s(u"query"), QUrl::FullyDecoded),
        s(u"artist:\"AC/DC & \\\"Friends\\\"\" AND release:\"Back\\\\Black #1 +=?\""));
    QCOMPARE(q.queryItems().size(), 3);
    QCOMPARE(q.queryItemValue(s(u"limit")), s(u"10"));
    // Non-Latin text survives; outer spaces are dropped.
    q = QUrlQuery(linuxbee::releaseSearchUrl(s(u"  Bj\u00f6rk "), s(u"\u30db\u30e2\u30b8\u30a7\u30cb\u30c3\u30af")));
    QCOMPARE(q.queryItemValue(s(u"query"), QUrl::FullyDecoded),
        s(u"artist:\"Bj\u00f6rk\" AND release:\"\u30db\u30e2\u30b8\u30a7\u30cb\u30c3\u30af\""));
    QCOMPARE(q.queryItemValue(s(u"limit")), s(u"10")); // the default
    // One of the two is enough; neither is no question.
    QCOMPARE(QUrlQuery(linuxbee::releaseSearchUrl(QString(), s(u"21"))).queryItemValue(s(u"query"), QUrl::FullyDecoded),
        s(u"release:\"21\""));
    QCOMPARE(QUrlQuery(linuxbee::releaseSearchUrl(s(u"Adele"), s(u"  "))).queryItemValue(s(u"query"), QUrl::FullyDecoded),
        s(u"artist:\"Adele\""));
    QVERIFY(!linuxbee::releaseSearchUrl(QString(), s(u" ")).isValid());
    // The limit is held to 1..100 (MB).
    QCOMPARE(QUrlQuery(linuxbee::releaseSearchUrl(s(u"a"), s(u"b"), 0)).queryItemValue(s(u"limit")), s(u"1"));
    QCOMPARE(QUrlQuery(linuxbee::releaseSearchUrl(s(u"a"), s(u"b"), 5000)).queryItemValue(s(u"limit")), s(u"100"));

    QCOMPARE(linuxbee::releaseLookupUrl(kId).toString(),
        s(u"https://musicbrainz.org/ws/2/release/e8c3ff98-75f0-383f-a7d1-35db67661d89"
          u"?inc=recordings+artist-credits+labels+release-groups&fmt=json"));
    QCOMPARE(linuxbee::frontCoverUrl(kId).toString(),
        s(u"https://coverartarchive.org/release/e8c3ff98-75f0-383f-a7d1-35db67661d89/front-500"));
    QCOMPARE(linuxbee::frontCoverUrl(kId, 0).toString(),
        s(u"https://coverartarchive.org/release/e8c3ff98-75f0-383f-a7d1-35db67661d89/front"));
    QCOMPARE(linuxbee::frontCoverUrl(kId, 1200).path(), s(u"/release/") + kId + s(u"/front-1200"));
    // An id is 36 characters of lower-case hex and hyphens, or it goes into no URL.
    for (const QString &bad : { QString(), s(u"abc"), kId.toUpper(), kId + s(u"/../x"), s(u"../../etc/passwd"),
             kId.left(35) + s(u"?") }) {
        QVERIFY(!linuxbee::releaseLookupUrl(bad).isValid());
        QVERIFY(!linuxbee::frontCoverUrl(bad).isValid());
    }
    QVERIFY(!linuxbee::frontCoverUrl(kId, 300).isValid());
    QVERIFY(!linuxbee::frontCoverUrl(kId, -1).isValid());
}

void MusicBrainzTest::search()
{
    // M-SEARCH
    const linuxbee::ReleaseSearch found = linuxbee::parseReleaseSearch(fixture("release-search.json"));
    QVERIFY2(found.ok, qPrintable(found.error));
    QCOMPARE(found.releases.size(), 3);
    const auto &a = found.releases.at(0);
    QCOMPARE(a.id, kId);
    QCOMPARE(a.title, s(u"OK Computer"));
    QCOMPARE(a.artist, s(u"Radiohead"));
    QCOMPARE(a.date, s(u"2009-04-01"));
    QCOMPARE(a.country, s(u"JP"));
    QCOMPARE(a.label, s(u"EMI"));
    QCOMPARE(a.catalogNumber, s(u"TOCP-70716")); // the first label entry
    QCOMPARE(a.format, s(u"2\u00d7CD"));
    QCOMPARE(a.status, s(u"Official"));
    QCOMPARE(a.trackCount, 27);
    QCOMPARE(a.discCount, 2);
    QCOMPARE(a.score, 100);
    const auto &c = found.releases.at(2);
    QCOMPARE(c.id, s(u"52709206-8816-3c12-9ff6-f957f2f1eecf"));
    QCOMPARE(c.date, s(u"1997-06-16"));
    QCOMPARE(c.country, s(u"XE"));
    QCOMPARE(c.label, s(u"Parlophone"));
    QCOMPARE(c.catalogNumber, s(u"8 55229 2"));
    QCOMPARE(c.format, s(u"CD"));
    QCOMPARE(c.trackCount, 12);
    QCOMPARE(c.discCount, 1);
    QCOMPARE(found.releases.at(1).label, s(u"Capitol Records"));

    // What may be missing is missing quietly; an entry without a usable id is dropped.
    const QByteArray sparse = "{\"releases\":[{\"id\":\"52709206-8816-3c12-9ff6-f957f2f1eecf\",\"title\":\"X\"},"
                              "{\"id\":\"not-an-id\",\"title\":\"Y\"},{\"title\":\"Z\"},"
                              "{\"id\":\"d4635a34-caf5-3b35-9a5b-d805f1126633\",\"score\":\"87\","
                              "\"artist-credit\":[{\"name\":\"A\",\"joinphrase\":\" feat. \"},{\"artist\":{\"name\":\"B\"}}],"
                              "\"media\":[{\"format\":\"CD\"},{\"format\":\"DVD\"},{\"format\":\"CD\"},{}]}]}";
    const linuxbee::ReleaseSearch few = linuxbee::parseReleaseSearch(sparse);
    QVERIFY(few.ok);
    QCOMPARE(few.releases.size(), 2);
    QCOMPARE(few.releases.at(0).title, s(u"X"));
    QVERIFY(few.releases.at(0).artist.isEmpty() && few.releases.at(0).format.isEmpty());
    QCOMPARE(few.releases.at(0).score, 0);
    QCOMPARE(few.releases.at(1).artist, s(u"A feat. B")); // credited name, else the artist's name
    QCOMPARE(few.releases.at(1).score, 87); // some answers give the score as text
    QCOMPARE(few.releases.at(1).format, s(u"2\u00d7CD + DVD + ?")); // grouped in order of first appearance
    QCOMPARE(few.releases.at(1).discCount, 4);
    const linuxbee::ReleaseSearch none = linuxbee::parseReleaseSearch("{\"count\":0,\"releases\":[]}");
    QVERIFY(none.ok);
    QVERIFY(none.releases.isEmpty());
}

void MusicBrainzTest::release()
{
    // M-RELEASE
    const Release r = linuxbee::parseRelease(fixture("release.json"));
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.id, kId);
    QCOMPARE(r.title, s(u"OK Computer"));
    QCOMPARE(r.artist, s(u"Radiohead"));
    QCOMPARE(r.date, s(u"2009-04-01"));
    QCOMPARE(r.originalDate, s(u"1997-05-21"));
    QCOMPARE(r.country, s(u"JP"));
    QCOMPARE(r.label, s(u"EMI"));
    QCOMPARE(r.catalogNumber, s(u"TOCP-70715"));
    QCOMPARE(r.discCount, 2);
    QVERIFY(r.hasFrontCover);
    QCOMPARE(r.tracks.size(), 27);
    QCOMPARE(r.tracksOnDisc(1), 12);
    QCOMPARE(r.tracksOnDisc(2), 15);
    QCOMPARE(r.tracksOnDisc(3), 0);
    const auto &first = r.tracks.at(0);
    QCOMPARE(first.disc, 1);
    QCOMPARE(first.position, 1);
    QCOMPARE(first.title, s(u"Airbag"));
    QCOMPARE(first.artist, s(u"Radiohead"));
    QCOMPARE(first.lengthMs, 284400);
    QCOMPARE(first.recordingId, s(u"4a7fea2e-545b-4c63-bc9a-9943cc3a29d7"));
    QCOMPARE(r.tracks.at(3).title, s(u"Exit Music (for a Film)"));
    QCOMPARE(r.tracks.at(11).title, s(u"The Tourist"));
    QCOMPARE(r.tracks.at(11).position, 12);
    const auto &second = r.tracks.at(12); // the second disc starts again at 1
    QCOMPARE(second.disc, 2);
    QCOMPARE(second.position, 1);
    QCOMPARE(second.title, s(u"Polyethylene, Parts 1 & 2"));
    QCOMPARE(second.lengthMs, 263946);
    // The track's own title, not the recording's ("Airbag").
    QCOMPARE(r.tracks.at(22).title, s(u"Airbag (live in Berlin)"));
    QCOMPARE(r.tracks.at(26).title, s(u"No Surprises (BBC Radio 1 Evening session 28/5/97)"));
    QCOMPARE(r.tracks.at(26).position, 15);

    // Fallbacks: the recording's title, artist and length; then the release's artist; position by counting.
    const QByteArray small = "{\"id\":\"52709206-8816-3c12-9ff6-f957f2f1eecf\",\"title\":\"T\","
                             "\"artist-credit\":[{\"name\":\"RA\"}],\"media\":["
                             "{\"tracks\":[{\"position\":7,\"recording\":{\"title\":\"From Recording\",\"length\":1000,"
                             "\"artist-credit\":[{\"name\":\"Rec Artist\"}]}},{\"title\":\"Own\",\"length\":null}]},"
                             "{\"tracks\":[]},{}]}";
    const Release f = linuxbee::parseRelease(small);
    QVERIFY2(f.ok, qPrintable(f.error));
    QCOMPARE(f.discCount, 3);
    QCOMPARE(f.tracks.size(), 2);
    QCOMPARE(f.tracks.at(0).title, s(u"From Recording"));
    QCOMPARE(f.tracks.at(0).artist, s(u"Rec Artist"));
    QCOMPARE(f.tracks.at(0).lengthMs, 1000);
    QCOMPARE(f.tracks.at(0).position, 1); // counted, whatever the answer claims
    QCOMPARE(f.tracks.at(1).title, s(u"Own"));
    QCOMPARE(f.tracks.at(1).artist, s(u"RA"));
    QCOMPARE(f.tracks.at(1).lengthMs, 0);
    QCOMPARE(f.tracks.at(1).position, 2);
    QVERIFY(!f.hasFrontCover);
    QVERIFY(f.label.isEmpty() && f.originalDate.isEmpty() && f.date.isEmpty());
}

void MusicBrainzTest::errors_data()
{
    QTest::addColumn<QByteArray>("json");
    QTest::addColumn<QString>("message"); // must appear in the error; empty: any message
    QTest::newRow("empty") << QByteArray() << QString();
    QTest::newRow("html") << QByteArray("<html><body>503</body></html>") << QString();
    QTest::newRow("array") << QByteArray("[]") << QString();
    QTest::newRow("busy") << QByteArray("{\"error\": \"The MusicBrainz web server is currently busy. Please try again later.\"}")
                          << s(u"currently busy");
    QTest::newRow("wrong-shape") << QByteArray("{\"count\":1}") << QString();
    QTest::newRow("truncated") << fixture("release.json").left(5000) << QString();
}

void MusicBrainzTest::errors()
{
    QFETCH(QByteArray, json);
    QFETCH(QString, message);
    const linuxbee::ReleaseSearch found = linuxbee::parseReleaseSearch(json);
    QVERIFY(!found.ok);
    QVERIFY(found.releases.isEmpty());
    QVERIFY(!found.error.isEmpty());
    QVERIFY2(found.error.contains(message), qPrintable(found.error)); // the service's own words are passed on
    const Release r = linuxbee::parseRelease(json);
    QVERIFY(!r.ok);
    QVERIFY(r.tracks.isEmpty() && r.id.isEmpty());
    QVERIFY(r.error.contains(message));
    // Nothing follows from a release that is not ok.
    QCOMPARE(linuxbee::matchTracks({ local(u"Airbag", u"1") }, r).at(0).releaseTrack, -1);
    QVERIFY(linuxbee::releaseTagChanges(local(u"Airbag").tags, r, 0).isEmpty());
}

void MusicBrainzTest::match()
{
    // M-MATCH: number and disc 3, title 4 (or 2 when one holds the other), length 2 (within 3 s) or 1 (within 10 s).
    const Release r = linuxbee::parseRelease(fixture("release.json"));
    QVERIFY(r.ok);
    const QList<LocalTrack> files = {
        local(u"Airbag", u"1", nullptr, 284), //  0: disc 1 track 1: 3 + 4 + 2 = 9
        local(u"paranoid ANDROID!", nullptr, nullptr, 0), //  1: title only: 4 -> track 2 (index 1)
        local(u"Untitled", u"05/12", u"1", 0), //  2: number only: 3 -> index 4 (Let Down)
        local(u"Polyethylene (Parts 1 & 2)", u"1", u"2", 264), //  3: disc 2: 3 + 4 + 2 -> index 12
        local(u"Lucky", nullptr, nullptr, 277), //  4: "Lucky" is index 10 (4 + 0); the live one is index 23 (2 + 2)
        local(u"Something Else Entirely", u"40", nullptr, 100), //  5: nothing
        local(u"Karma Police", nullptr, nullptr, 262), //  6: 4 + 2 -> index 5
        local(u"Karma Police", nullptr, nullptr, 0), //  7: 4, but index 5 is taken by the better fit
        local(nullptr, nullptr, nullptr, 117), //  8: a length alone (2) is not enough
        local(u"Climbing Up the Walls", u"7", u"2", 319), //  9: disc 2 track 7 is the Zero 7 mix: 3 + 2 + 2 = 7; the album cut scores 4
        local(u"No Surprises", u"10", nullptr, 229), // 10: no disc tag means disc 1: 3 + 4 + 2 -> index 9
    };
    const QList<linuxbee::TrackMatch> m = linuxbee::matchTracks(files, r);
    QCOMPARE(m.size(), files.size());
    const QList<int> expected = { 0, 1, 4, 12, 10, -1, 5, -1, -1, 18, 9 };
    const QList<int> scores = { 9, 4, 3, 9, 4, 0, 6, 0, 0, 7, 9 };
    for (int i = 0; i < m.size(); ++i) {
        QVERIFY2(m.at(i).releaseTrack == expected.at(i),
            qPrintable(QStringLiteral("file %1: got track %2, want %3").arg(i).arg(m.at(i).releaseTrack).arg(expected.at(i))));
        QVERIFY2(m.at(i).score == scores.at(i),
            qPrintable(QStringLiteral("file %1: got score %2, want %3").arg(i).arg(m.at(i).score).arg(scores.at(i))));
    }
    // Equal scores: the earlier file gets the track. The later one gets nothing:
    // "Lucky (live in Florence)" only holds its title, and 2 is under the 3 a match needs.
    const QList<linuxbee::TrackMatch> tie = linuxbee::matchTracks({ local(u"Lucky"), local(u"Lucky") }, r);
    QCOMPARE(tie.size(), 2);
    QCOMPARE(tie.at(0).releaseTrack, 10);
    QCOMPARE(tie.at(0).score, 4);
    QCOMPARE(tie.at(1).releaseTrack, -1);
    QCOMPARE(tie.at(1).score, 0);
    // A better fit later in the list still wins over a worse one earlier.
    const QList<linuxbee::TrackMatch> better = linuxbee::matchTracks({ local(u"x", u"3"), local(u"Subterranean Homesick Alien", u"3") }, r);
    QCOMPARE(better.at(0).releaseTrack, -1);
    QCOMPARE(better.at(1).releaseTrack, 2);
    QCOMPARE(better.at(1).score, 7);
    QVERIFY(linuxbee::matchTracks({ }, r).isEmpty());
}

void MusicBrainzTest::changes()
{
    // M-CHANGES
    const Release r = linuxbee::parseRelease(fixture("release.json"));
    QVERIFY(r.ok);
    linuxbee::TagChanges c = linuxbee::releaseTagChanges(linuxbee::TrackTags(), r, 12);
    QCOMPARE(c.fields,
        Map({ { s(u"Title"), { s(u"Polyethylene, Parts 1 & 2") } }, { s(u"Artist"), { s(u"Radiohead") } },
            { s(u"Album"), { s(u"OK Computer") } }, { s(u"Album Artist"), { s(u"Radiohead") } },
            { s(u"Year"), { s(u"2009-04-01") } }, { s(u"Original Year"), { s(u"1997-05-21") } },
            { s(u"Track#"), { s(u"1") } }, { s(u"Track Count"), { s(u"15") } }, { s(u"Disc#"), { s(u"2") } },
            { s(u"Disc Count"), { s(u"2") } }, { s(u"Publisher"), { s(u"EMI") } } }));
    QVERIFY(c.other.isEmpty() && !c.pictures.has_value());
    // Only what differs is named.
    linuxbee::TrackTags have;
    have.fields = { { s(u"Title"), { s(u"airbag") } }, { s(u"Artist"), { s(u"Radiohead") } },
        { s(u"Album"), { s(u"OK Computer") } }, { s(u"Album Artist"), { s(u"Radiohead") } },
        { s(u"Year"), { s(u"1997") } }, { s(u"Track#"), { s(u"01") } }, { s(u"Genre"), { s(u"Rock") } },
        { s(u"Disc#"), { s(u"1") } }, { s(u"Disc Count"), { s(u"2") } } };
    c = linuxbee::releaseTagChanges(have, r, 0);
    QCOMPARE(c.fields,
        Map({ { s(u"Title"), { s(u"Airbag") } }, { s(u"Year"), { s(u"2009-04-01") } },
            { s(u"Original Year"), { s(u"1997-05-21") } }, { s(u"Track#"), { s(u"1") } },
            { s(u"Track Count"), { s(u"12") } }, { s(u"Publisher"), { s(u"EMI") } } }));
    // Blank fields only: a field that has any value is left alone.
    linuxbee::AutoTagOptions blank;
    blank.blankFieldsOnly = true;
    c = linuxbee::releaseTagChanges(have, r, 0, blank);
    QCOMPARE(c.fields,
        Map({ { s(u"Original Year"), { s(u"1997-05-21") } }, { s(u"Track Count"), { s(u"12") } },
            { s(u"Publisher"), { s(u"EMI") } } }));
    // A track index that is not in the release: nothing.
    QVERIFY(linuxbee::releaseTagChanges(have, r, 27).isEmpty());
    QVERIFY(linuxbee::releaseTagChanges(have, r, -1).isEmpty());
    // What the release does not say is not written, and never clears a field.
    const Release bare = linuxbee::parseRelease(
        "{\"id\":\"52709206-8816-3c12-9ff6-f957f2f1eecf\",\"title\":\"T\",\"media\":[{\"tracks\":[{\"title\":\"One\"}]}]}");
    QVERIFY(bare.ok);
    c = linuxbee::releaseTagChanges(have, bare, 0);
    QCOMPARE(c.fields,
        Map({ { s(u"Title"), { s(u"One") } }, { s(u"Album"), { s(u"T") } }, { s(u"Track#"), { s(u"1") } },
            { s(u"Track Count"), { s(u"1") } }, { s(u"Disc Count"), { s(u"1") } } }));
}

QTEST_GUILESS_MAIN(MusicBrainzTest)
#include "tst_musicbrainz.moc"
