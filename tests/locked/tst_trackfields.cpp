// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Where each expected value comes from:
//   W-SF  MusicBee wiki, "Special Fields"
//         https://musicbee.fandom.com/wiki/Special_Fields
//   W-H   MusicBee wiki, "How to Create a File-Naming Template" (the example
//         path Adele\21\01-11 Someone like You.mp3)
//   F-x   a rule in docs/spec/track-fields.md, worked by hand beside the row
#include <QtTest>

#include "fields/trackfields.h"

using linuxbee::TrackFieldSource;
using linuxbee::TrackTags;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

TrackTags adele()
{
    TrackTags t;
    t.format = s(u"mp3");
    t.audio.durationMs = 285240;
    t.audio.bitrateKbps = 320;
    t.audio.sampleRateHz = 44100;
    t.audio.channels = 2;
    t.fields = {
        { s(u"Title"), { s(u"Someone like You") } },
        { s(u"Artist"), { s(u"Adele") } },
        { s(u"Album Artist"), { s(u"Adele") } },
        { s(u"Album"), { s(u"21") } },
        { s(u"Year"), { s(u"2011-01-24") } },
        { s(u"Track#"), { s(u"11") } },
        { s(u"Disc#"), { s(u"1") } },
    };
    return t;
}

const QString kPath = QStringLiteral("/music/Adele/21/11 Someone like You.MP3");

QString get(const TrackTags &tags, const QString &path, const QString &field)
{
    const TrackFieldSource source(tags, path);
    const std::optional<QString> v = source.value(field);
    return v ? *v : QStringLiteral("<unknown>");
}

}

class TrackFieldsTest : public QObject {
    Q_OBJECT
private slots:
    void names();
    void tagFields();
    void computed_data();
    void computed();
    void discTrack_data();
    void discTrack();
    void years_data();
    void years();
    void fileFields_data();
    void fileFields();
    void audioFields();
    void formatDuration_data();
    void formatDuration();
    void inTemplates();
};

void TrackFieldsTest::names()
{
    // F-NAMES: the tag catalogue, then the computed fields in this order.
    const QStringList computed = { s(u"Disc-Track#"), s(u"Year (yyyy)"), s(u"Decade"),
        s(u"Album [Year]"), s(u"Display Artist"), s(u"Artist & Title"), s(u"ArtistAndTitle"),
        s(u"Filename"), s(u".Ext"), s(u"Path"), s(u"URL"), s(u"Kind"), s(u"Time"), s(u"Bitrate"),
        s(u"Sample Rate"), s(u"Channels") };
    QCOMPARE(linuxbee::trackFieldNames(), linuxbee::tagFieldNames() + computed);
}

void TrackFieldsTest::tagFields()
{
    TrackTags t = adele();
    t.fields.insert(s(u"Genre"), { s(u"Pop"), s(u"Soul") });
    // F-TAGS: as R-SOURCE in docs/spec/tag-reading.md.
    QCOMPARE(get(t, kPath, s(u"Title")), s(u"Someone like You"));
    QCOMPARE(get(t, kPath, s(u"genre")), s(u"Pop; Soul"));
    QCOMPARE(get(t, kPath, s(u"Conductor")), QString()); // known, blank
    QCOMPARE(get(t, kPath, s(u"Nope")), s(u"<unknown>"));
    QCOMPARE(get(t, kPath, s(u"")), s(u"<unknown>"));
}

void TrackFieldsTest::computed_data()
{
    QTest::addColumn<QString>("field");
    QTest::addColumn<QString>("expected");
    // W-H: disc 1, track 11 gives 01-11.
    QTest::newRow("disc-track") << s(u"Disc-Track#") << s(u"01-11");
    // W-SF: "Equivalent to $Date(<Year>,yyyy)".
    QTest::newRow("year-yyyy") << s(u"Year (yyyy)") << s(u"2011");
    QTest::newRow("decade") << s(u"Decade") << s(u"2010s"); // F-DECADE
    // W-SF: "Equivalent to the template <Album> [$Date(<Year>,yyyy)]".
    QTest::newRow("album-year") << s(u"Album [Year]") << s(u"21 [2011]");
    // W-SF: "<Artist> - <Title> when the tags are set". Both spellings.
    QTest::newRow("artist-title") << s(u"Artist & Title") << s(u"Adele - Someone like You");
    QTest::newRow("artistandtitle") << s(u"ArtistAndTitle") << s(u"Adele - Someone like You");
    QTest::newRow("display-artist") << s(u"Display Artist") << s(u"Adele"); // F-DISPLAY
    QTest::newRow("case") << s(u"disc-track#") << s(u"01-11"); // F-NAMES: case is ignored
}

void TrackFieldsTest::computed()
{
    QFETCH(QString, field);
    QFETCH(QString, expected);
    QCOMPARE(get(adele(), kPath, field), expected);
}

void TrackFieldsTest::discTrack_data()
{
    QTest::addColumn<QString>("disc");
    QTest::addColumn<QString>("track");
    QTest::addColumn<QString>("expected");
    // F-DISCTRACK
    QTest::newRow("both") << s(u"1") << s(u"11") << s(u"01-11"); // W-H
    QTest::newRow("small-track") << s(u"2") << s(u"3") << s(u"02-03");
    QTest::newRow("no-disc") << QString() << s(u"7") << s(u"07");
    QTest::newRow("no-track") << s(u"1") << QString() << QString();
    QTest::newRow("neither") << QString() << QString() << QString();
    QTest::newRow("already-padded") << s(u"01") << s(u"007") << s(u"01-07");
    QTest::newRow("three-digits") << s(u"1") << s(u"123") << s(u"01-123");
    QTest::newRow("spaces") << s(u" 1 ") << s(u" 4 ") << s(u"01-04");
    // A number that is not a whole number is used as written.
    QTest::newRow("vinyl") << s(u"1") << s(u"A1") << s(u"01-A1");
}

void TrackFieldsTest::discTrack()
{
    QFETCH(QString, disc);
    QFETCH(QString, track);
    QFETCH(QString, expected);
    TrackTags t;
    if (!disc.isEmpty())
        t.fields.insert(s(u"Disc#"), { disc });
    if (!track.isEmpty())
        t.fields.insert(s(u"Track#"), { track });
    QCOMPARE(get(t, kPath, s(u"Disc-Track#")), expected);

    // F-DISCTRACK: the widths are options.
    linuxbee::TrackFieldOptions options;
    options.discDigits = 1;
    options.trackDigits = 3;
    const TrackFieldSource source(t, kPath, options);
    if (disc == s(u"2") && track == s(u"3"))
        QCOMPARE(source.value(s(u"Disc-Track#")).value_or(s(u"?")), s(u"2-003"));
}

void TrackFieldsTest::years_data()
{
    QTest::addColumn<QString>("year");
    QTest::addColumn<QString>("yyyy");
    QTest::addColumn<QString>("decade");
    QTest::addColumn<QString>("albumYear");
    // F-YEAR: the first run of exactly four digits.
    QTest::newRow("plain") << s(u"1997") << s(u"1997") << s(u"1990s") << s(u"X [1997]");
    QTest::newRow("date") << s(u"2011-01-24") << s(u"2011") << s(u"2010s") << s(u"X [2011]");
    QTest::newRow("day-first") << s(u"24/01/2011") << s(u"2011") << s(u"2010s") << s(u"X [2011]");
    QTest::newRow("2000") << s(u"2000") << s(u"2000") << s(u"2000s") << s(u"X [2000]");
    QTest::newRow("1969") << s(u"1969") << s(u"1969") << s(u"1960s") << s(u"X [1969]");
    QTest::newRow("blank") << QString() << QString() << QString() << s(u"X");
    QTest::newRow("words") << s(u"unknown") << QString() << QString() << s(u"X");
    QTest::newRow("two-digits") << s(u"97") << QString() << QString() << s(u"X");
    QTest::newRow("five-digits") << s(u"19975") << QString() << QString() << s(u"X");
}

void TrackFieldsTest::years()
{
    QFETCH(QString, year);
    QFETCH(QString, yyyy);
    QFETCH(QString, decade);
    QFETCH(QString, albumYear);
    TrackTags t;
    t.fields.insert(s(u"Album"), { s(u"X") });
    if (!year.isEmpty())
        t.fields.insert(s(u"Year"), { year });
    QCOMPARE(get(t, kPath, s(u"Year (yyyy)")), yyyy);
    QCOMPARE(get(t, kPath, s(u"Decade")), decade);
    QCOMPARE(get(t, kPath, s(u"Album [Year]")), albumYear);
    // No album: blank, never " [1997]".
    t.fields.remove(s(u"Album"));
    QCOMPARE(get(t, kPath, s(u"Album [Year]")), QString());
}

void TrackFieldsTest::fileFields_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<QString>("filename");
    QTest::addColumn<QString>("ext");
    QTest::addColumn<QString>("dir");
    // W-SF: Filename is "without directory but with file extension"; .Ext has
    // "no leading ."; Path is the "full directory path, except filename"; URL
    // is the full path. F-FILE settles case, the trailing separator and edges.
    QTest::newRow("usual") << kPath << s(u"11 Someone like You.MP3") << s(u"MP3")
                           << s(u"/music/Adele/21");
    QTest::newRow("two-dots") << s(u"/m/T.N.T.flac") << s(u"T.N.T.flac") << s(u"flac") << s(u"/m");
    QTest::newRow("no-ext") << s(u"/m/README") << s(u"README") << QString() << s(u"/m");
    QTest::newRow("hidden") << s(u"/m/.hidden") << s(u".hidden") << QString() << s(u"/m");
    QTest::newRow("root") << s(u"/a.ogg") << s(u"a.ogg") << s(u"ogg") << s(u"/");
    QTest::newRow("dotted-folder") << s(u"/m/v1.0/track") << s(u"track") << QString() << s(u"/m/v1.0");
    QTest::newRow("empty") << QString() << QString() << QString() << QString();
}

void TrackFieldsTest::fileFields()
{
    QFETCH(QString, path);
    QFETCH(QString, filename);
    QFETCH(QString, ext);
    QFETCH(QString, dir);
    const TrackTags t = adele();
    QCOMPARE(get(t, path, s(u"Filename")), filename);
    QCOMPARE(get(t, path, s(u".Ext")), ext);
    QCOMPARE(get(t, path, s(u"Path")), dir);
    QCOMPARE(get(t, path, s(u"URL")), path);

    // W-SF: Artist & Title "otherwise ... will show <Filename>".
    TrackTags untitled = adele();
    untitled.fields.remove(s(u"Title"));
    QCOMPARE(get(untitled, path, s(u"Artist & Title")), filename);
    QCOMPARE(get(untitled, path, s(u"ArtistAndTitle")), filename);
}

void TrackFieldsTest::audioFields()
{
    TrackTags t = adele();
    // W-SF: Kind is e.g. "MPEG audio file". F-KIND has the rest of the table.
    QCOMPARE(get(t, kPath, s(u"Kind")), s(u"MPEG audio file"));
    QCOMPARE(get(t, kPath, s(u"Time")), s(u"4:45")); // F-TIME: 285 240 ms is 285 s
    QCOMPARE(get(t, kPath, s(u"Bitrate")), s(u"320k"));
    QCOMPARE(get(t, kPath, s(u"Sample Rate")), s(u"44.1 kHz"));
    QCOMPARE(get(t, kPath, s(u"Channels")), s(u"Stereo"));

    const QList<QPair<QString, QString>> kinds = {
        { s(u"flac"), s(u"FLAC audio file") },
        { s(u"ogg"), s(u"Ogg Vorbis audio file") },
        { s(u"opus"), s(u"Opus audio file") },
        { s(u"m4a"), s(u"MPEG-4 audio file") },
        { s(u"wav"), s(u"WAV audio file") },
        { s(u"wv"), s(u"WavPack audio file") },
        { s(u"ape"), s(u"APE audio file") }, // F-KIND: any other format
    };
    for (const auto &[format, kind] : kinds) {
        t.format = format;
        QCOMPARE(get(t, kPath, s(u"Kind")), kind);
    }
    t.audio.sampleRateHz = 48000;
    t.audio.channels = 1;
    QCOMPARE(get(t, kPath, s(u"Sample Rate")), s(u"48 kHz"));
    QCOMPARE(get(t, kPath, s(u"Channels")), s(u"Mono"));
    t.audio.channels = 6;
    QCOMPARE(get(t, kPath, s(u"Channels")), s(u"6"));

    // F-AUDIO: tags that did not come from a file have blank audio fields.
    const TrackTags none;
    for (const QString &field : { s(u"Kind"), s(u"Time"), s(u"Bitrate"), s(u"Sample Rate"), s(u"Channels") })
        QCOMPARE(get(none, kPath, field), QString());
}

void TrackFieldsTest::formatDuration_data()
{
    QTest::addColumn<int>("ms");
    QTest::addColumn<QString>("text");
    // F-TIME: rounded to the nearest second, halves upward.
    QTest::newRow("0") << 0 << s(u"0:00");
    QTest::newRow("250") << 250 << s(u"0:00");
    QTest::newRow("499") << 499 << s(u"0:00");
    QTest::newRow("500") << 500 << s(u"0:01");
    QTest::newRow("7s") << 7000 << s(u"0:07");
    QTest::newRow("59.4") << 59400 << s(u"0:59");
    QTest::newRow("59.5") << 59500 << s(u"1:00");
    QTest::newRow("3:05") << 185000 << s(u"3:05");
    QTest::newRow("59:59") << 3599000 << s(u"59:59");
    QTest::newRow("1h") << 3600000 << s(u"1:00:00");
    QTest::newRow("1:02:03") << 3723000 << s(u"1:02:03");
    QTest::newRow("negative") << -5 << s(u"0:00");
}

void TrackFieldsTest::formatDuration()
{
    QFETCH(int, ms);
    QFETCH(QString, text);
    QCOMPARE(linuxbee::formatDuration(ms), text);
}

void TrackFieldsTest::inTemplates()
{
    const TrackTags t = adele();
    const TrackFieldSource source(t, kPath);
    // W-H: the default naming template gives the wiki's example name.
    const linuxbee::Template naming
        = linuxbee::Template::parse(s(uR"~(<Album Artist>\<Album>\<Disc-Track#> <Title>)~"));
    QCOMPARE(naming.evaluate(source), s(uR"~(Adele\21\01-11 Someone like You)~"));
    const linuxbee::Template other
        = linuxbee::Template::parse(s(u"<Decade>/<Album [Year]>/<Filename> (<.Ext>, <Time>)"));
    QCOMPARE(other.evaluate(source),
        s(u"2010s/21 [2011]/11 Someone like You.MP3 (MP3, 4:45)"));
}

QTEST_APPLESS_MAIN(TrackFieldsTest)
#include "tst_trackfields.moc"
