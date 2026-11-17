// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Where each expected value comes from:
//   GEN   tests/data/tags/make_fixtures.py wrote this value into the fixture
//         with the mutagen library (not the library the program uses).
//         tests/data/tags/MANIFEST.txt is mutagen's and ffprobe's read-back.
//   R-x   a rule in docs/spec/tag-reading.md
//   MATH  worked in the comment
#include <QtTest>

#include "tags/tagreader.h"

using linuxbee::ReadResult;
using linuxbee::TrackTags;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

QString fixture(const char *name)
{
    return QStringLiteral(LINUXBEE_TEST_DATA "/tags/") + QLatin1String(name);
}

TrackTags read(const char *name)
{
    const ReadResult result = linuxbee::readTags(fixture(name));
    if (result.status != ReadResult::Status::Ok)
        return { };
    return result.tags;
}

// GEN: png_1x1() in make_fixtures.py. 69 bytes.
QByteArray png()
{
    return QByteArray::fromHex(
        "89504e470d0a1a0a0000000d4948445200000001000000010802000000907753de"
        "0000000c49444154789c63f8cfc0000003010100c9fe92ef0000000049454e44ae426082");
}

using Fields = QMap<QString, QStringList>;

}

class TagReaderTest : public QObject {
    Q_OBJECT
private slots:
    void catalogue();
    void mp3Id3v24();
    void mp3Id3v23();
    void mp3Id3v1Only();
    void flac();
    void emptyFlac();
    void oggVorbis();
    void opus();
    void m4a();
    void wav();
    void wavpack();
    void pictures();
    void pictureOptions();
    void notMusic_data();
    void notMusic();
    void detectedByContent();
    void ratingFromPopm_data();
    void ratingFromPopm();
    void ratingFromText_data();
    void ratingFromText();
    void ratingToString();
    void imageMimeType();
    void helpers();
    void templateSource();
};

void TagReaderTest::catalogue()
{
    // R-CATALOGUE: the exact list and order.
    const QStringList expected = { s(u"Title"), s(u"Artist"), s(u"Album"), s(u"Album Artist"),
        s(u"Year"), s(u"Track#"), s(u"Track Count"), s(u"Disc#"), s(u"Disc Count"), s(u"Genre"),
        s(u"Composer"), s(u"Conductor"), s(u"Publisher"), s(u"Grouping"), s(u"Comment"),
        s(u"Rating"), s(u"BPM"), s(u"Mood"), s(u"Lyricist"), s(u"Lyrics"), s(u"Compilation"),
        s(u"Copyright"), s(u"ISRC"), s(u"Sort Title"), s(u"Sort Artist"), s(u"Sort Album"),
        s(u"Sort Album Artist"), s(u"Sort Composer"), s(u"Original Year"), s(u"Original Artist"),
        s(u"Original Album"), s(u"Subtitle"), s(u"Language"), s(u"Initial Key"), s(u"Encoded By"),
        s(u"Mix Artist"), s(u"Love") };
    QCOMPARE(linuxbee::tagFieldNames(), expected);
}

void TagReaderTest::mp3Id3v24()
{
    const ReadResult result = linuxbee::readTags(fixture("basic.mp3"));
    QCOMPARE(result.status, ReadResult::Status::Ok);
    const TrackTags &t = result.tags;
    QCOMPARE(t.format, s(u"mp3")); // R-FORMAT
    QCOMPARE(t.tagTypes, QStringList({ s(u"ID3v2.4") })); // GEN: v2_version=4, v1=0

    // GEN, with R-NUMBERS (TRCK "11/12", TPOS "1/2"), R-RATING (POPM 196 is
    // 4 stars) and R-GROUPING (TIT1 is Grouping in an ID3 tag).
    const Fields expected = {
        { s(u"Title"), { s(u"Someone like You") } },
        { s(u"Artist"), { s(u"Adele") } },
        { s(u"Album Artist"), { s(u"Adele") } },
        { s(u"Album"), { s(u"21") } },
        { s(u"Year"), { s(u"2011-01-24") } },
        { s(u"Track#"), { s(u"11") } },
        { s(u"Track Count"), { s(u"12") } },
        { s(u"Disc#"), { s(u"1") } },
        { s(u"Disc Count"), { s(u"2") } },
        { s(u"Genre"), { s(u"Pop"), s(u"Soul") } },
        { s(u"Composer"), { s(u"Adele Adkins"), s(u"Dan Wilson") } },
        { s(u"BPM"), { s(u"135") } },
        { s(u"Grouping"), { s(u"Ballads") } },
        { s(u"Comment"), { s(u"A comment") } },
        { s(u"Lyrics"), { s(u"I heard\nthat you're settled down") } },
        { s(u"Mood"), { s(u"Sad") } },
        { s(u"Rating"), { s(u"4") } },
    };
    QCOMPARE(t.fields, expected);
    // R-OTHER: a text tag with no field keeps its own key.
    QCOMPARE(t.other, Fields({ { s(u"MY CUSTOM"), { s(u"kept") } } }));
    // R-UNSUPPORTED: pictures and the rating frame are reported elsewhere.
    QCOMPARE(t.unsupported, QStringList());

    // R-AUDIO. The encoder pads an MP3, so the length is a range.
    QVERIFY2(t.audio.durationMs >= 240 && t.audio.durationMs <= 320,
        qPrintable(QString::number(t.audio.durationMs)));
    QCOMPARE(t.audio.bitrateKbps, 128); // GEN: -b:a 128k
    QCOMPARE(t.audio.sampleRateHz, 44100);
    QCOMPARE(t.audio.channels, 2);
    QCOMPARE(t.audio.bitsPerSample, 0);
}

void TagReaderTest::mp3Id3v23()
{
    const TrackTags t = read("v23.mp3");
    // R-TAGTYPES order: ID3v2, then APEv2, then ID3v1. GEN: v2_version=3, v1=2.
    QCOMPARE(t.tagTypes, QStringList({ s(u"ID3v2.3"), s(u"ID3v1") }));
    // GEN: UTF-16 text. The ID3v2 tag wins over the ID3v1 tag (R-PRECEDENCE),
    // whose 30 Latin-1 bytes could not hold this title.
    const Fields expected = {
        { s(u"Title"), { s(u"\u00dcn\u00efc\u00f6d\u00e9 \u65e5\u672c\u8a9e") } },
        { s(u"Artist"), { s(u"Bj\u00f6rk") } },
        { s(u"Album"), { s(u"Homogenic") } },
        // GEN: TYER "1997" and TDAT "2209". ID3: TDAT is day then month. R-DATE23.
        { s(u"Year"), { s(u"1997-09-22") } },
        { s(u"Track#"), { s(u"3") } },
        { s(u"Rating"), { s(u"3") } }, // GEN: POPM 128
    };
    QCOMPARE(t.fields, expected);
    QVERIFY(t.other.isEmpty());
    QCOMPARE(t.unsupported, QStringList()); // R-DATE23: TDAT is part of Year
}

void TagReaderTest::mp3Id3v1Only()
{
    const TrackTags t = read("v1only.mp3");
    QCOMPARE(t.tagTypes, QStringList({ s(u"ID3v1") }));
    // GEN: the 128 bytes written by hand. Genre byte 17 is "Rock" in the
    // ID3v1 genre list.
    const Fields expected = {
        { s(u"Title"), { s(u"V1 Title") } },
        { s(u"Artist"), { s(u"V1 Artist") } },
        { s(u"Album"), { s(u"V1 Album") } },
        { s(u"Year"), { s(u"1985") } },
        { s(u"Track#"), { s(u"7") } },
        { s(u"Genre"), { s(u"Rock") } },
        { s(u"Comment"), { s(u"V1 comment") } },
    };
    QCOMPARE(t.fields, expected);
}

void TagReaderTest::flac()
{
    const TrackTags t = read("basic.flac");
    QCOMPARE(t.format, s(u"flac"));
    QCOMPARE(t.tagTypes, QStringList({ s(u"Vorbis") }));
    // GEN, with R-FIELDS (ORGANIZATION is Publisher, CONTENTGROUP is
    // Grouping, UNSYNCEDLYRICS is Lyrics, TRACKTOTAL is Track Count) and
    // R-RATING (RATING "80" is 4 stars).
    const Fields expected = {
        { s(u"Title"), { s(u"Flac Title") } },
        { s(u"Artist"), { s(u"First Artist"), s(u"Second Artist") } },
        { s(u"Album"), { s(u"Flac Album") } },
        { s(u"Album Artist"), { s(u"Various Artists") } },
        { s(u"Year"), { s(u"2011") } },
        { s(u"Track#"), { s(u"5") } },
        { s(u"Track Count"), { s(u"10") } },
        { s(u"Disc#"), { s(u"2") } },
        { s(u"Disc Count"), { s(u"3") } },
        { s(u"Genre"), { s(u"Rock"), s(u"Blues") } },
        { s(u"Publisher"), { s(u"A Label") } },
        { s(u"Grouping"), { s(u"A Group") } },
        { s(u"Compilation"), { s(u"1") } },
        { s(u"Rating"), { s(u"4") } },
        { s(u"Lyrics"), { s(u"la la") } },
    };
    QCOMPARE(t.fields, expected);
    QCOMPARE(t.other, Fields({ { s(u"REPLAYGAIN_TRACK_GAIN"), { s(u"-6.50 dB") } } }));
    QCOMPARE(t.unsupported, QStringList());

    QCOMPARE(t.audio.durationMs, 250); // GEN: -t 0.25, FLAC is exact
    QCOMPARE(t.audio.sampleRateHz, 44100);
    QCOMPARE(t.audio.channels, 2);
    QCOMPARE(t.audio.bitsPerSample, 16);
    QVERIFY(t.audio.bitrateKbps > 0);
}

void TagReaderTest::emptyFlac()
{
    // GEN: audio with every tag and picture removed. Still a music file.
    const ReadResult result = linuxbee::readTags(fixture("empty.flac"));
    QCOMPARE(result.status, ReadResult::Status::Ok);
    QVERIFY(result.tags.fields.isEmpty());
    QVERIFY(result.tags.other.isEmpty());
    QVERIFY(result.tags.pictures.isEmpty());
    QCOMPARE(result.tags.audio.durationMs, 250);
}

void TagReaderTest::oggVorbis()
{
    const TrackTags t = read("basic.ogg");
    QCOMPARE(t.format, s(u"ogg"));
    QCOMPARE(t.tagTypes, QStringList({ s(u"Vorbis") }));
    // GEN: the other spelling of the totals, TOTALTRACKS and TOTALDISCS.
    const Fields expected = {
        { s(u"Title"), { s(u"Ogg Title") } },
        { s(u"Artist"), { s(u"Ogg Artist") } },
        { s(u"Track#"), { s(u"4") } },
        { s(u"Track Count"), { s(u"9") } },
        { s(u"Disc#"), { s(u"1") } },
        { s(u"Disc Count"), { s(u"1") } },
    };
    QCOMPARE(t.fields, expected);
    // R-OTHER: the picture field is a picture, not a text tag.
    QVERIFY(t.other.isEmpty());
    QCOMPARE(t.audio.durationMs, 250);
    QCOMPARE(t.audio.sampleRateHz, 44100);
}

void TagReaderTest::opus()
{
    const TrackTags t = read("basic.opus");
    QCOMPARE(t.format, s(u"opus"));
    const Fields expected = {
        { s(u"Title"), { s(u"Opus Title") } },
        { s(u"Artist"), { s(u"Opus Artist") } },
        { s(u"Album"), { s(u"Opus Album") } },
        { s(u"Year"), { s(u"2020-05-06") } },
        { s(u"Track#"), { s(u"2") } }, // GEN: TRACKNUMBER "2/8", R-NUMBERS
        { s(u"Track Count"), { s(u"8") } },
    };
    QCOMPARE(t.fields, expected);
    QCOMPARE(t.audio.sampleRateHz, 48000); // R-AUDIO: Opus always decodes at 48 kHz
    QCOMPARE(t.audio.channels, 2);
}

void TagReaderTest::m4a()
{
    const TrackTags t = read("basic.m4a");
    QCOMPARE(t.format, s(u"m4a"));
    QCOMPARE(t.tagTypes, QStringList({ s(u"MP4") }));
    // GEN: trkn (3, 9), disk (1, 2), cpil true, tmpo 128.
    const Fields expected = {
        { s(u"Title"), { s(u"M4A Title") } },
        { s(u"Artist"), { s(u"M4A Artist") } },
        { s(u"Album"), { s(u"M4A Album") } },
        { s(u"Album Artist"), { s(u"M4A Album Artist") } },
        { s(u"Year"), { s(u"2015") } },
        { s(u"Genre"), { s(u"Electronic") } },
        { s(u"Composer"), { s(u"M4A Composer") } },
        { s(u"Track#"), { s(u"3") } },
        { s(u"Track Count"), { s(u"9") } },
        { s(u"Disc#"), { s(u"1") } },
        { s(u"Disc Count"), { s(u"2") } },
        { s(u"Compilation"), { s(u"1") } },
        { s(u"BPM"), { s(u"128") } },
    };
    QCOMPARE(t.fields, expected);
    QCOMPARE(t.unsupported, QStringList());
    QCOMPARE(t.audio.sampleRateHz, 44100);
    QCOMPARE(t.audio.channels, 2);
}

void TagReaderTest::wav()
{
    const TrackTags t = read("basic.wav");
    QCOMPARE(t.format, s(u"wav"));
    QCOMPARE(t.tagTypes, QStringList({ s(u"ID3v2.4") })); // GEN: mutagen's default
    const Fields expected = {
        { s(u"Title"), { s(u"Wav Title") } },
        { s(u"Artist"), { s(u"Wav Artist") } },
        { s(u"Track#"), { s(u"1") } },
    };
    QCOMPARE(t.fields, expected);
    QCOMPARE(t.audio.durationMs, 250);
    QCOMPARE(t.audio.bitsPerSample, 16);
    // MATH: 44100 Hz x 16 bits x 2 channels = 1 411 200 bits per second.
    QCOMPARE(t.audio.bitrateKbps, 1411);
}

void TagReaderTest::wavpack()
{
    const TrackTags t = read("basic.wv");
    QCOMPARE(t.format, s(u"wv"));
    QCOMPARE(t.tagTypes, QStringList({ s(u"APEv2") }));
    // GEN: APE keys Year, Track "6/7" and Disc.
    const Fields expected = {
        { s(u"Title"), { s(u"Wv Title") } },
        { s(u"Artist"), { s(u"Wv Artist") } },
        { s(u"Album"), { s(u"Wv Album") } },
        { s(u"Year"), { s(u"2001") } },
        { s(u"Track#"), { s(u"6") } },
        { s(u"Track Count"), { s(u"7") } },
        { s(u"Disc#"), { s(u"1") } },
    };
    QCOMPARE(t.fields, expected);
    QCOMPARE(t.unsupported, QStringList()); // R-UNSUPPORTED: the picture item is a picture
    QCOMPARE(t.audio.durationMs, 250);
    QCOMPARE(t.audio.bitsPerSample, 16);
}

void TagReaderTest::pictures()
{
    // GEN: the same 69-byte PNG in every file that has a picture.
    for (const char *name : { "basic.mp3", "basic.flac", "basic.ogg", "basic.m4a", "basic.wv" }) {
        const TrackTags t = read(name);
        QVERIFY2(t.pictures.size() == 1, name);
        const linuxbee::Picture &p = t.pictures.first();
        QVERIFY2(p.data == png(), name);
        QVERIFY2(p.byteCount == 69, name);
        // R-MIME: an APE picture stores no type of image; the bytes say PNG.
        QVERIFY2(p.mimeType == s(u"image/png"), name);
        // R-PICTURES: MP4 stores no type; a picture with none is a front cover.
        QVERIFY2(p.type == s(u"Front Cover"), name);
    }
    QCOMPARE(read("basic.flac").pictures.first().description, s(u"front"));
    QCOMPARE(read("basic.mp3").pictures.first().description, s(u"front"));
    QCOMPARE(read("basic.m4a").pictures.first().description, QString());
    QVERIFY(read("basic.opus").pictures.isEmpty());
    QVERIFY(read("v23.mp3").pictures.isEmpty());
}

void TagReaderTest::pictureOptions()
{
    linuxbee::ReadOptions noData;
    noData.pictureData = false;
    const ReadResult a = linuxbee::readTags(fixture("basic.flac"), noData);
    QCOMPARE(a.tags.pictures.size(), 1);
    QVERIFY(a.tags.pictures.first().data.isEmpty());
    QCOMPARE(a.tags.pictures.first().byteCount, 69);
    QCOMPARE(a.tags.pictures.first().mimeType, s(u"image/png"));

    linuxbee::ReadOptions none;
    none.pictures = false;
    const ReadResult b = linuxbee::readTags(fixture("basic.flac"), none);
    QCOMPARE(b.status, ReadResult::Status::Ok);
    QVERIFY(b.tags.pictures.isEmpty());
    QCOMPARE(b.tags.first(s(u"Title")), s(u"Flac Title"));
}

void TagReaderTest::notMusic_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<ReadResult::Status>("status");
    // R-STATUS
    QTest::newRow("missing") << fixture("no-such-file.mp3") << ReadResult::Status::NotFound;
    QTest::newRow("directory") << QStringLiteral(LINUXBEE_TEST_DATA "/tags")
                               << ReadResult::Status::NotFound;
    QTest::newRow("empty-path") << QString() << ReadResult::Status::NotFound;
    QTest::newRow("text-named-mp3") << fixture("notaudio.mp3") << ReadResult::Status::NotAudio;
    QTest::newRow("zero-bytes") << fixture("zero.mp3") << ReadResult::Status::NotAudio;
    QTest::newRow("truncated") << fixture("truncated.flac") << ReadResult::Status::NotAudio;
    QTest::newRow("python-file") << fixture("make_fixtures.py") << ReadResult::Status::NotAudio;
}

void TagReaderTest::notMusic()
{
    QFETCH(QString, path);
    QFETCH(ReadResult::Status, status);
    const ReadResult result = linuxbee::readTags(path);
    QCOMPARE(result.status, status);
    QVERIFY(result.tags.fields.isEmpty());
    QVERIFY(result.tags.format.isEmpty());
}

void TagReaderTest::detectedByContent()
{
    // GEN: a copy of basic.flac with no extension. R-FORMAT: the content decides.
    const ReadResult result = linuxbee::readTags(fixture("noextension"));
    QCOMPARE(result.status, ReadResult::Status::Ok);
    QCOMPARE(result.tags.format, s(u"flac"));
    QCOMPARE(result.tags.first(s(u"Title")), s(u"Flac Title"));
}

void TagReaderTest::ratingFromPopm_data()
{
    QTest::addColumn<int>("popm");
    QTest::addColumn<double>("stars"); // 0 stands for "no rating"
    // R-RATING: the anchor table.
    QTest::newRow("0") << 0 << 0.0;
    QTest::newRow("1") << 1 << 1.0;
    QTest::newRow("13") << 13 << 0.5;
    QTest::newRow("54") << 54 << 1.5;
    QTest::newRow("64") << 64 << 2.0;
    QTest::newRow("118") << 118 << 2.5;
    QTest::newRow("128") << 128 << 3.0;
    QTest::newRow("186") << 186 << 3.5;
    QTest::newRow("196") << 196 << 4.0;
    QTest::newRow("242") << 242 << 4.5;
    QTest::newRow("255") << 255 << 5.0;
    // R-RATING: any other value takes the nearest anchor.
    QTest::newRow("2") << 2 << 0.5; // MATH: nearest of 13, 54, ... is 13
    QTest::newRow("100") << 100 << 2.5; // MATH: 118-100=18 beats 100-64=36
    QTest::newRow("224") << 224 << 4.5; // MATH: 242-224=18 beats 224-196=28
    QTest::newRow("250") << 250 << 5.0; // MATH: 255-250=5 beats 250-242=8
    // R-RATING: a tie goes to the higher rating. MATH: 59-54 = 64-59 = 5.
    QTest::newRow("59") << 59 << 2.0;
    // Out of range.
    QTest::newRow("-1") << -1 << 0.0;
    QTest::newRow("256") << 256 << 0.0;
}

void TagReaderTest::ratingFromPopm()
{
    QFETCH(int, popm);
    QFETCH(double, stars);
    const std::optional<double> got = linuxbee::ratingFromPopm(popm);
    if (stars == 0.0)
        QVERIFY(!got.has_value());
    else
        QCOMPARE(got.value_or(-1), stars);
}

void TagReaderTest::ratingFromText_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<double>("stars");
    // R-RATING: above 5 the number is a percentage (stars = n / 20); up to 5
    // it is stars. Rounded to the nearest half, halves upward.
    QTest::newRow("100") << s(u"100") << 5.0;
    QTest::newRow("80") << s(u"80") << 4.0;
    QTest::newRow("90") << s(u"90") << 4.5;
    QTest::newRow("50") << s(u"50") << 2.5;
    QTest::newRow("5") << s(u"5") << 5.0;
    QTest::newRow("4") << s(u"4") << 4.0;
    QTest::newRow("4.5") << s(u"4.5") << 4.5;
    QTest::newRow("spaces") << s(u" 3 ") << 3.0;
    QTest::newRow("7") << s(u"7") << 0.5; // MATH: 7/20 = 0.35, nearest half is 0.5
    QTest::newRow("6") << s(u"6") << 0.5; // MATH: 0.3 would round to 0; the least is 0.5
    QTest::newRow("0.1") << s(u"0.1") << 0.5;
    QTest::newRow("75") << s(u"75") << 4.0; // MATH: 3.75 -> 7.5 halves -> 8 halves
    QTest::newRow("0") << s(u"0") << 0.0;
    QTest::newRow("empty") << QString() << 0.0;
    QTest::newRow("words") << s(u"five") << 0.0;
    QTest::newRow("101") << s(u"101") << 0.0;
    QTest::newRow("negative") << s(u"-3") << 0.0;
}

void TagReaderTest::ratingFromText()
{
    QFETCH(QString, text);
    QFETCH(double, stars);
    const std::optional<double> got = linuxbee::ratingFromText(text);
    if (stars == 0.0)
        QVERIFY(!got.has_value());
    else
        QCOMPARE(got.value_or(-1), stars);
}

void TagReaderTest::ratingToString()
{
    QCOMPARE(linuxbee::ratingToString(4.0), s(u"4"));
    QCOMPARE(linuxbee::ratingToString(3.5), s(u"3.5"));
    QCOMPARE(linuxbee::ratingToString(0.5), s(u"0.5"));
    QCOMPARE(linuxbee::ratingToString(5.0), s(u"5"));
}

void TagReaderTest::imageMimeType()
{
    // R-MIME: the first bytes of each format, from its own specification.
    QCOMPARE(linuxbee::imageMimeType(png()), s(u"image/png"));
    QCOMPARE(linuxbee::imageMimeType(QByteArray::fromHex("ffd8ffe000104a464946")), s(u"image/jpeg"));
    QCOMPARE(linuxbee::imageMimeType(QByteArray("GIF89a....")), s(u"image/gif"));
    QCOMPARE(linuxbee::imageMimeType(QByteArray("GIF87a....")), s(u"image/gif"));
    QCOMPARE(linuxbee::imageMimeType(QByteArray("BM......")), s(u"image/bmp"));
    QCOMPARE(linuxbee::imageMimeType(QByteArray("RIFF\x10\0\0\0WEBPVP8 ", 16)), s(u"image/webp"));
    QCOMPARE(linuxbee::imageMimeType(QByteArray("RIFF\x10\0\0\0WAVEfmt ", 16)), QString());
    QCOMPARE(linuxbee::imageMimeType(QByteArray("hello")), QString());
    QCOMPARE(linuxbee::imageMimeType(QByteArray()), QString());
}

void TagReaderTest::helpers()
{
    const TrackTags t = read("basic.flac");
    QCOMPARE(t.first(s(u"Artist")), s(u"First Artist"));
    QCOMPARE(t.joined(s(u"Artist")), s(u"First Artist; Second Artist"));
    QCOMPARE(t.first(s(u"Conductor")), QString());
    QCOMPARE(t.joined(s(u"Conductor")), QString());
}

void TagReaderTest::templateSource()
{
    const TrackTags t = read("basic.flac");
    const linuxbee::TrackTagSource source(t);
    // R-SOURCE
    QCOMPARE(source.value(s(u"Title")).value_or(s(u"?")), s(u"Flac Title"));
    QCOMPARE(source.value(s(u"Artist")).value_or(s(u"?")), s(u"First Artist; Second Artist"));
    QCOMPARE(source.value(s(u"album artist")).value_or(s(u"?")), s(u"Various Artists"));
    // A catalogue field with no value is known and blank.
    QVERIFY(source.value(s(u"Conductor")).has_value());
    QCOMPARE(source.value(s(u"Conductor")).value_or(s(u"?")), QString());
    // Anything else is unknown, including keys that are only in `other`.
    QVERIFY(!source.value(s(u"REPLAYGAIN_TRACK_GAIN")).has_value());
    QVERIFY(!source.value(s(u"Nope")).has_value());

    const linuxbee::Template tmpl
        = linuxbee::Template::parse(s(u"<Album Artist> - <Album> - <Track#> <Title>"));
    QCOMPARE(tmpl.evaluate(source), s(u"Various Artists - Flac Album - 5 Flac Title"));
}

QTEST_APPLESS_MAIN(TagReaderTest)
#include "tst_tagreader.moc"
