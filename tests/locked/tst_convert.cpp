// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Where each expected value comes from:
//   FF    the ffmpeg manuals (ffmpeg.org/ffmpeg.html, ffmpeg-codecs.html,
//         ffmpeg-formats.html) for the option named
//   GEN   tests/data/convert/make_fixtures.py wrote this into the fixture with
//         mutagen; MANIFEST.txt beside it is ffprobe's read-back
//   BYTES the test looks at the raw bytes of the result
//   C-x   a rule in docs/spec/convert.md
// tests/locked/cli_convert.sh checks the same conversions with ffprobe and
// with decoded-audio checksums, so the audio is never judged by this program.
#include <QtTest>

#include "convert/converter.h"

using linuxbee::AudioFormat;
using linuxbee::ConvertJob;
using linuxbee::ConvertResult;
using linuxbee::EncoderSettings;
using Fields = QMap<QString, QStringList>;
using RC = linuxbee::EncoderSettings::RateControl;

Q_DECLARE_METATYPE(linuxbee::EncoderSettings)

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

QString fixture(const char *name)
{
    return QStringLiteral(LINUXBEE_TEST_DATA "/convert/") + QLatin1String(name);
}

QByteArray bytesOf(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArrayLiteral("<cannot open>");
    return file.readAll();
}

EncoderSettings preset(const char *id)
{
    const auto p = linuxbee::encoderPreset(QLatin1String(id));
    return p ? p->settings : EncoderSettings();
}

EncoderSettings make(AudioFormat format, RC rc = RC::Bitrate, int bitrate = 192, double quality = 0)
{
    EncoderSettings e;
    e.format = format;
    e.rateControl = rc;
    e.bitrateKbps = bitrate;
    e.quality = quality;
    return e;
}

// GEN: what tone.flac holds, in linuxBee's field names.
Fields toneFields()
{
    return {
        { s(u"Title"), { s(u"Tone Title") } },
        { s(u"Artist"), { s(u"Tone Artist"), s(u"Second Artist") } },
        { s(u"Album Artist"), { s(u"Tone Artist") } },
        { s(u"Album"), { s(u"Tone Album") } },
        { s(u"Year"), { s(u"2001-02-03") } },
        { s(u"Track#"), { s(u"3") } },
        { s(u"Track Count"), { s(u"12") } },
        { s(u"Disc#"), { s(u"1") } },
        { s(u"Genre"), { s(u"Test") } },
        { s(u"Comment"), { s(u"A \u00fcnicode comment \u65e5\u672c") } },
        { s(u"Rating"), { s(u"4") } },
    };
}

const QStringList kHead = { s(u"-nostdin"), s(u"-hide_banner"), s(u"-loglevel"), s(u"error"),
    s(u"-nostats"), s(u"-progress"), s(u"pipe:1"), s(u"-y"), s(u"-i"), s(u"/in/a b.flac"),
    s(u"-map"), s(u"0:a:0"), s(u"-map_metadata"), s(u"-1"), s(u"-map_chapters"), s(u"-1"),
    s(u"-fflags"), s(u"+bitexact") };

}

class ConvertTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void formats();
    void presets();
    void validate_data();
    void validate();
    void arguments_data();
    void arguments();
    void everyPreset_data();
    void everyPreset();
    void mp3Result();
    void lossless_data();
    void lossless();
    void lossySource();
    void tagOptions();
    void progress();
    void ifExists();
    void refusals();
    void encoderProblems();
    void cancel();

private:
    QString out(const char *name);
    QString script(const char *name, const char *body);
    QStringList leftovers();
    QTemporaryDir m_dir;
    QString m_work;
    int m_counter = 0;
};

void ConvertTest::init()
{
    QVERIFY(m_dir.isValid());
    m_work = m_dir.filePath(QStringLiteral("t%1").arg(++m_counter));
    QVERIFY(QDir().mkpath(m_work));
}

QString ConvertTest::out(const char *name)
{
    return m_work + QLatin1Char('/') + QLatin1String(name);
}

// A stand-in for ffmpeg, so that failure and slowness can be staged.
QString ConvertTest::script(const char *name, const char *body)
{
    const QString path = m_dir.filePath(QLatin1String(name));
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write("#!/bin/sh\n");
        file.write(body);
        file.close();
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    }
    return path;
}

// Everything in the work folder, sorted. C-CLEAN is checked with this.
QStringList ConvertTest::leftovers()
{
    QStringList names;
    QDirIterator it(m_work, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext())
        names.append(QDir(m_work).relativeFilePath(it.next()));
    names.sort();
    return names;
}

void ConvertTest::formats()
{
    struct Row {
        AudioFormat format;
        const char *id;
        const char *extension;
    };
    // C-FORMATS
    const Row rows[] = {
        { AudioFormat::Mp3, "mp3", "mp3" },
        { AudioFormat::Flac, "flac", "flac" },
        { AudioFormat::OggVorbis, "ogg", "ogg" },
        { AudioFormat::Opus, "opus", "opus" },
        { AudioFormat::Aac, "aac", "m4a" },
        { AudioFormat::Alac, "alac", "m4a" },
        { AudioFormat::Wav, "wav", "wav" },
        { AudioFormat::WavPack, "wavpack", "wv" },
    };
    for (const Row &row : rows) {
        QCOMPARE(linuxbee::formatId(row.format), QLatin1String(row.id));
        QCOMPARE(linuxbee::fileExtension(row.format), QLatin1String(row.extension));
        const auto back = linuxbee::formatFromId(QLatin1String(row.id));
        QVERIFY(back.has_value());
        QVERIFY(*back == row.format);
    }
    QVERIFY(linuxbee::formatFromId(s(u"FLAC")).has_value()); // case is ignored
    QVERIFY(!linuxbee::formatFromId(s(u"wma")).has_value());
    QVERIFY(!linuxbee::formatFromId(QString()).has_value());
}

void ConvertTest::presets()
{
    // C-PRESETS: ids in display order. The first is the default.
    const QStringList ids = { s(u"mp3-320"), s(u"mp3-v0"), s(u"mp3-v2"), s(u"mp3-192"),
        s(u"mp3-128"), s(u"flac"), s(u"flac-8"), s(u"flac-cd"), s(u"ogg-q5"), s(u"ogg-q8"),
        s(u"opus-128"), s(u"opus-96"), s(u"opus-192"), s(u"aac-256"), s(u"aac-160"), s(u"alac"),
        s(u"wav"), s(u"wavpack") };
    QStringList got;
    for (const linuxbee::EncoderPreset &p : linuxbee::encoderPresets()) {
        got.append(p.id);
        QVERIFY2(!p.name.isEmpty(), qPrintable(p.id));
        QVERIFY2(linuxbee::validateSettings(p.settings).isEmpty(), qPrintable(p.id));
    }
    QCOMPARE(got, ids);
    QCOMPARE(linuxbee::encoderPreset(s(u"mp3-320")).value_or(linuxbee::EncoderPreset()).name,
        s(u"MP3 320 kbps (CBR)"));

    const EncoderSettings mp3 = preset("mp3-320");
    QVERIFY(mp3.format == AudioFormat::Mp3);
    QVERIFY(mp3.rateControl == RC::Bitrate);
    QCOMPARE(mp3.bitrateKbps, 320);
    QCOMPARE(mp3.sampleRateHz, 0);
    QCOMPARE(mp3.channels, 0);
    const EncoderSettings v0 = preset("mp3-v0");
    QVERIFY(v0.rateControl == RC::Quality);
    QCOMPARE(v0.quality, 0.0);
    QCOMPARE(preset("mp3-v2").quality, 2.0);
    QCOMPARE(preset("flac").compressionLevel, 5);
    QCOMPARE(preset("flac-8").compressionLevel, 8);
    QCOMPARE(preset("flac-cd").sampleRateHz, 44100);
    QCOMPARE(preset("flac-cd").bitDepth, 16);
    QCOMPARE(preset("ogg-q5").quality, 5.0);
    QVERIFY(preset("ogg-q5").rateControl == RC::Quality);
    QCOMPARE(preset("opus-96").bitrateKbps, 96);
    QCOMPARE(preset("aac-256").bitrateKbps, 256);
    QVERIFY(preset("alac").format == AudioFormat::Alac);
    QVERIFY(!linuxbee::encoderPreset(s(u"nope")).has_value());
}

void ConvertTest::validate_data()
{
    QTest::addColumn<EncoderSettings>("settings");
    QTest::addColumn<QString>("problemStartsWith"); // empty: valid
    auto with = [](EncoderSettings e, int rate, int depth, int channels, int level) {
        e.sampleRateHz = rate;
        e.bitDepth = depth;
        e.channels = channels;
        e.compressionLevel = level;
        return e;
    };
    const EncoderSettings mp3 = make(AudioFormat::Mp3, RC::Bitrate, 320);
    const EncoderSettings flac = make(AudioFormat::Flac);
    // C-VALID
    QTest::newRow("mp3-320") << mp3 << QString();
    QTest::newRow("mp3-300") << make(AudioFormat::Mp3, RC::Bitrate, 300) << s(u"bitrate:");
    QTest::newRow("mp3-32") << make(AudioFormat::Mp3, RC::Bitrate, 32) << QString();
    QTest::newRow("mp3-q0") << make(AudioFormat::Mp3, RC::Quality, 0, 0) << QString();
    QTest::newRow("mp3-q9") << make(AudioFormat::Mp3, RC::Quality, 0, 9) << QString();
    QTest::newRow("mp3-q10") << make(AudioFormat::Mp3, RC::Quality, 0, 10) << s(u"quality:");
    QTest::newRow("mp3-q2.5") << make(AudioFormat::Mp3, RC::Quality, 0, 2.5) << s(u"quality:");
    QTest::newRow("ogg-q-1") << make(AudioFormat::OggVorbis, RC::Quality, 0, -1) << QString();
    QTest::newRow("ogg-q6.5") << make(AudioFormat::OggVorbis, RC::Quality, 0, 6.5) << QString();
    QTest::newRow("ogg-q11") << make(AudioFormat::OggVorbis, RC::Quality, 0, 11) << s(u"quality:");
    QTest::newRow("ogg-192k") << make(AudioFormat::OggVorbis, RC::Bitrate, 192) << QString();
    QTest::newRow("opus-6") << make(AudioFormat::Opus, RC::Bitrate, 6) << QString();
    QTest::newRow("opus-510") << make(AudioFormat::Opus, RC::Bitrate, 510) << QString();
    QTest::newRow("opus-5") << make(AudioFormat::Opus, RC::Bitrate, 5) << s(u"bitrate:");
    QTest::newRow("opus-quality") << make(AudioFormat::Opus, RC::Quality, 128, 5) << s(u"quality:");
    QTest::newRow("aac-quality") << make(AudioFormat::Aac, RC::Quality, 128, 2) << s(u"quality:");
    QTest::newRow("aac-16") << make(AudioFormat::Aac, RC::Bitrate, 16) << s(u"bitrate:");
    QTest::newRow("mp3-depth") << with(mp3, 0, 16, 0, -1) << s(u"bit depth:");
    QTest::newRow("mp3-96k") << with(mp3, 96000, 0, 0, -1) << s(u"sample rate:");
    QTest::newRow("mp3-48k") << with(mp3, 48000, 0, 0, -1) << QString();
    QTest::newRow("mp3-44099") << with(mp3, 44099, 0, 0, -1) << s(u"sample rate:");
    QTest::newRow("mp3-level") << with(mp3, 0, 0, 0, 5) << s(u"compression level:");
    QTest::newRow("mp3-mono") << with(mp3, 0, 0, 1, -1) << QString();
    QTest::newRow("mp3-3ch") << with(mp3, 0, 0, 3, -1) << s(u"channels:");
    QTest::newRow("opus-44100") << with(make(AudioFormat::Opus, RC::Bitrate, 128), 44100, 0, 0, -1)
                                << s(u"sample rate:");
    QTest::newRow("opus-48000") << with(make(AudioFormat::Opus, RC::Bitrate, 128), 48000, 0, 0, -1)
                                << QString();
    // Lossless formats ignore bitrate and quality altogether.
    QTest::newRow("flac") << make(AudioFormat::Flac, RC::Quality, 7, 99) << QString();
    QTest::newRow("flac-12") << with(flac, 0, 0, 0, 12) << QString();
    QTest::newRow("flac-13") << with(flac, 0, 0, 0, 13) << s(u"compression level:");
    QTest::newRow("flac-24-192k") << with(flac, 192000, 24, 0, -1) << QString();
    QTest::newRow("flac-20bit") << with(flac, 0, 20, 0, -1) << s(u"bit depth:");
    QTest::newRow("alac-32") << with(make(AudioFormat::Alac), 0, 32, 0, -1) << s(u"bit depth:");
    QTest::newRow("wav-32") << with(make(AudioFormat::Wav), 0, 32, 0, -1) << QString();
    QTest::newRow("wavpack-level") << with(make(AudioFormat::WavPack), 0, 0, 0, 3)
                                   << s(u"compression level:");
}

void ConvertTest::validate()
{
    QFETCH(EncoderSettings, settings);
    QFETCH(QString, problemStartsWith);
    const QStringList problems = linuxbee::validateSettings(settings);
    if (problemStartsWith.isEmpty()) {
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('|'))));
    } else {
        QCOMPARE(problems.size(), 1);
        QVERIFY2(problems.first().startsWith(problemStartsWith), qPrintable(problems.first()));
    }
}

void ConvertTest::arguments_data()
{
    QTest::addColumn<EncoderSettings>("settings");
    QTest::addColumn<int>("sourceBits");
    QTest::addColumn<QStringList>("tail"); // what follows kHead
    auto with = [](EncoderSettings e, int rate, int depth, int channels, int level) {
        e.sampleRateHz = rate;
        e.bitDepth = depth;
        e.channels = channels;
        e.compressionLevel = level;
        return e;
    };
    const QString o = s(u"/out/x.part.ext");
    auto l = [](std::initializer_list<const char16_t *> items) {
        QStringList list;
        for (const char16_t *item : items)
            list.append(QString::fromUtf16(item));
        return list;
    };
    // C-ARGS. FF: libmp3lame "b" is the bitrate for CBR; "q" is LAME's -V.
    QTest::newRow("mp3-320") << make(AudioFormat::Mp3, RC::Bitrate, 320) << 16
                             << l({ u"-c:a", u"libmp3lame", u"-b:a", u"320k", u"-f", u"mp3", u"/out/x.part.ext" });
    QTest::newRow("mp3-v0") << make(AudioFormat::Mp3, RC::Quality, 320, 0) << 16
                            << l({ u"-c:a", u"libmp3lame", u"-q:a", u"0", u"-f", u"mp3", u"/out/x.part.ext" });
    QTest::newRow("mp3-128-mono-22k")
        << with(make(AudioFormat::Mp3, RC::Bitrate, 128), 22050, 0, 1, -1) << 0
        << l({ u"-c:a", u"libmp3lame", u"-b:a", u"128k", u"-ar", u"22050", u"-ac", u"1", u"-f", u"mp3",
               u"/out/x.part.ext" });
    // FF: libvorbis "q" from -1 to 10.
    QTest::newRow("ogg-q6.5") << make(AudioFormat::OggVorbis, RC::Quality, 0, 6.5) << 16
                              << l({ u"-c:a", u"libvorbis", u"-q:a", u"6.5", u"-f", u"ogg", u"/out/x.part.ext" });
    QTest::newRow("ogg-192") << make(AudioFormat::OggVorbis, RC::Bitrate, 192) << 16
                             << l({ u"-c:a", u"libvorbis", u"-b:a", u"192k", u"-f", u"ogg", u"/out/x.part.ext" });
    QTest::newRow("opus-128") << make(AudioFormat::Opus, RC::Bitrate, 128) << 24
                              << l({ u"-c:a", u"libopus", u"-b:a", u"128k", u"-f", u"opus", u"/out/x.part.ext" });
    // FF: the "ipod" muxer writes .m4a.
    QTest::newRow("aac-256") << make(AudioFormat::Aac, RC::Bitrate, 256) << 16
                             << l({ u"-c:a", u"aac", u"-b:a", u"256k", u"-f", u"ipod", u"/out/x.part.ext" });
    // C-DEPTH: a lossless target keeps 16, 24 or 32 bits and otherwise uses 16.
    QTest::newRow("flac-from-16") << with(make(AudioFormat::Flac), 0, 0, 0, 5) << 16
                                  << l({ u"-c:a", u"flac", u"-compression_level", u"5", u"-sample_fmt",
                                         u"s16", u"-f", u"flac", u"/out/x.part.ext" });
    QTest::newRow("flac-from-24") << make(AudioFormat::Flac) << 24
                                  << l({ u"-c:a", u"flac", u"-sample_fmt", u"s32", u"-bits_per_raw_sample",
                                         u"24", u"-f", u"flac", u"/out/x.part.ext" });
    QTest::newRow("flac-from-32") << make(AudioFormat::Flac) << 32
                                  << l({ u"-c:a", u"flac", u"-sample_fmt", u"s32", u"-f", u"flac",
                                         u"/out/x.part.ext" });
    QTest::newRow("flac-from-lossy") << make(AudioFormat::Flac) << 0
                                     << l({ u"-c:a", u"flac", u"-sample_fmt", u"s16", u"-f", u"flac",
                                            u"/out/x.part.ext" });
    QTest::newRow("flac-from-8") << make(AudioFormat::Flac) << 8
                                 << l({ u"-c:a", u"flac", u"-sample_fmt", u"s16", u"-f", u"flac",
                                        u"/out/x.part.ext" });
    QTest::newRow("flac-cd-from-24") << with(make(AudioFormat::Flac), 44100, 16, 0, 5) << 24
                                     << l({ u"-c:a", u"flac", u"-compression_level", u"5", u"-sample_fmt",
                                            u"s16", u"-ar", u"44100", u"-f", u"flac", u"/out/x.part.ext" });
    QTest::newRow("alac-from-16") << make(AudioFormat::Alac) << 16
                                  << l({ u"-c:a", u"alac", u"-sample_fmt", u"s16p", u"-f", u"ipod",
                                         u"/out/x.part.ext" });
    QTest::newRow("alac-from-24") << make(AudioFormat::Alac) << 24
                                  << l({ u"-c:a", u"alac", u"-sample_fmt", u"s32p", u"-bits_per_raw_sample",
                                         u"24", u"-f", u"ipod", u"/out/x.part.ext" });
    // C-DEPTH: Apple Lossless stops at 24.
    QTest::newRow("alac-from-32") << make(AudioFormat::Alac) << 32
                                  << l({ u"-c:a", u"alac", u"-sample_fmt", u"s32p", u"-bits_per_raw_sample",
                                         u"24", u"-f", u"ipod", u"/out/x.part.ext" });
    QTest::newRow("wavpack-from-24") << make(AudioFormat::WavPack) << 24
                                     << l({ u"-c:a", u"wavpack", u"-sample_fmt", u"s32p",
                                            u"-bits_per_raw_sample", u"24", u"-f", u"wv", u"/out/x.part.ext" });
    // FF: PCM depth is chosen by codec name.
    QTest::newRow("wav-from-16") << make(AudioFormat::Wav) << 16
                                 << l({ u"-c:a", u"pcm_s16le", u"-f", u"wav", u"/out/x.part.ext" });
    QTest::newRow("wav-from-24") << make(AudioFormat::Wav) << 24
                                 << l({ u"-c:a", u"pcm_s24le", u"-f", u"wav", u"/out/x.part.ext" });
    QTest::newRow("wav-32-stereo") << with(make(AudioFormat::Wav), 0, 32, 2, -1) << 16
                                   << l({ u"-c:a", u"pcm_s32le", u"-ac", u"2", u"-f", u"wav",
                                          u"/out/x.part.ext" });
    Q_UNUSED(o);
}

void ConvertTest::arguments()
{
    QFETCH(EncoderSettings, settings);
    QFETCH(int, sourceBits);
    QFETCH(QStringList, tail);
    QCOMPARE(linuxbee::ffmpegArguments(s(u"/in/a b.flac"), s(u"/out/x.part.ext"), settings, sourceBits),
        kHead + tail);
}

void ConvertTest::everyPreset_data()
{
    QTest::addColumn<QString>("id");
    QTest::addColumn<QString>("readerFormat");
    const QList<QPair<const char *, const char *>> rows = {
        { "mp3-320", "mp3" }, { "mp3-v0", "mp3" }, { "mp3-v2", "mp3" }, { "mp3-192", "mp3" },
        { "mp3-128", "mp3" }, { "flac", "flac" }, { "flac-8", "flac" }, { "flac-cd", "flac" },
        { "ogg-q5", "ogg" }, { "ogg-q8", "ogg" }, { "opus-128", "opus" }, { "opus-96", "opus" },
        { "opus-192", "opus" }, { "aac-256", "m4a" }, { "aac-160", "m4a" }, { "alac", "m4a" },
        { "wav", "wav" }, { "wavpack", "wv" },
    };
    for (const auto &[id, format] : rows)
        QTest::newRow(id) << QString::fromLatin1(id) << QString::fromLatin1(format);
}

void ConvertTest::everyPreset()
{
    QFETCH(QString, id);
    QFETCH(QString, readerFormat);
    const auto p = linuxbee::encoderPreset(id);
    QVERIFY(p.has_value());
    ConvertJob job;
    job.source = fixture("tone.flac");
    job.destination = m_work + QStringLiteral("/sub/dir/out.") + linuxbee::fileExtension(p->settings.format);
    job.settings = p->settings;
    const QByteArray sourceBefore = bytesOf(job.source);

    const ConvertResult result = linuxbee::convertFile(job);
    QVERIFY2(result.status == ConvertResult::Status::Converted, qPrintable(result.message));
    QVERIFY(result.ok());
    QCOMPARE(result.outputPath, job.destination); // C-STEPS: the folders were created
    QCOMPARE(result.warnings, QStringList());
    // C-CLEAN: the result and nothing else.
    QCOMPARE(leftovers(), QStringList({ QStringLiteral("sub/dir/out.") + linuxbee::fileExtension(p->settings.format) }));
    QCOMPARE(bytesOf(job.source), sourceBefore); // the source is never written to

    const linuxbee::ReadResult read = linuxbee::readTags(result.outputPath);
    QCOMPARE(read.status, linuxbee::ReadResult::Status::Ok);
    const linuxbee::TrackTags &t = read.tags;
    QCOMPARE(t.format, readerFormat);
    QCOMPARE(t.audio.channels, 2);
    // GEN: one second. Lossy encoders pad a little.
    QVERIFY2(qAbs(t.audio.durationMs - 1000) <= 80, qPrintable(QString::number(t.audio.durationMs)));
    // C-TAGS: every field, the picture and the other tags came across (GEN).
    QCOMPARE(t.fields, toneFields());
    QCOMPARE(t.other, Fields({ { s(u"MY KEY"), { s(u"my value") } } })); // ENCODER is not copied
    QCOMPARE(t.pictures.size(), 1);
    QCOMPARE(t.pictures.first().mimeType, s(u"image/png"));
    QCOMPARE(t.pictures.first().byteCount, 69);
}

void ConvertTest::mp3Result()
{
    ConvertJob job;
    job.source = fixture("tone.flac");
    job.destination = out("a.mp3");
    job.settings = preset("mp3-320");
    QCOMPARE(linuxbee::convertFile(job).status, ConvertResult::Status::Converted);
    const linuxbee::TrackTags t = linuxbee::readTags(job.destination).tags;
    // The exact figure is checked with ffprobe in cli_convert.sh. On a one-second
    // file the tag library's average includes the header frame.
    QVERIFY2(qAbs(t.audio.bitrateKbps - 320) <= 10, qPrintable(QString::number(t.audio.bitrateKbps)));
    QCOMPARE(t.audio.sampleRateHz, 44100);
    // C-TAGS: job.tagOptions decides the tag kind. Defaults: a new tag is ID3v2.4 (W-ID3VER), and ID3v1.
    QCOMPARE(t.tagTypes, QStringList({ s(u"ID3v2.4"), s(u"ID3v1") }));
    const QByteArray b = bytesOf(job.destination);
    QVERIFY(b.startsWith(QByteArray("ID3\x04\x00", 5))); // BYTES
    // BYTES: GEN's RATING 80 is 4 stars, which the tag writer stores as POPM byte 196.
    QVERIFY(b.contains(QByteArray("POPM\0\0\0\x06\0\0\0\xC4\0\0\0\0", 16)));
    QVERIFY(b.right(128).startsWith("TAGTone Title"));

    // 192 kbps is 192, and the tag options are honoured.
    job.destination = out("b.mp3");
    job.settings = preset("mp3-192");
    job.tagOptions.id3Version = linuxbee::WriteOptions::Id3Version::V24;
    job.tagOptions.writeId3v1 = false;
    QCOMPARE(linuxbee::convertFile(job).status, ConvertResult::Status::Converted);
    const linuxbee::TrackTags u = linuxbee::readTags(job.destination).tags;
    QVERIFY2(qAbs(u.audio.bitrateKbps - 192) <= 10, qPrintable(QString::number(u.audio.bitrateKbps)));
    QCOMPARE(u.tagTypes, QStringList({ s(u"ID3v2.4") }));

    // A 96 kHz source: MP3 cannot hold it, and ffmpeg resamples to 48 kHz.
    job.source = fixture("hires.flac");
    job.destination = out("c.mp3");
    job.settings = preset("mp3-320");
    QCOMPARE(linuxbee::convertFile(job).status, ConvertResult::Status::Converted);
    QCOMPARE(linuxbee::readTags(job.destination).tags.audio.sampleRateHz, 48000);
    QCOMPARE(linuxbee::readTags(job.destination).tags.first(s(u"Title")), s(u"Hi Res")); // GEN
}

void ConvertTest::lossless_data()
{
    QTest::addColumn<QString>("id");
    QTest::addColumn<QString>("source");
    QTest::addColumn<int>("bits");
    QTest::addColumn<int>("rate");
    // C-DEPTH. GEN: tone.flac is 16-bit 44100 Hz, hires.flac is 24-bit 96000 Hz.
    for (const char *id : { "flac", "alac", "wav", "wavpack" }) {
        QTest::addRow("%s-16", id) << QString::fromLatin1(id) << s(u"tone.flac") << 16 << 44100;
        QTest::addRow("%s-24", id) << QString::fromLatin1(id) << s(u"hires.flac") << 24 << 96000;
    }
    QTest::newRow("flac-cd-from-hires") << s(u"flac-cd") << s(u"hires.flac") << 16 << 44100;
}

void ConvertTest::lossless()
{
    QFETCH(QString, id);
    QFETCH(QString, source);
    QFETCH(int, bits);
    QFETCH(int, rate);
    ConvertJob job;
    job.source = fixture(qPrintable(source));
    const auto p = linuxbee::encoderPreset(id);
    QVERIFY(p.has_value());
    job.settings = p->settings;
    job.destination = out("x.") + linuxbee::fileExtension(job.settings.format);
    const ConvertResult result = linuxbee::convertFile(job);
    QVERIFY2(result.status == ConvertResult::Status::Converted, qPrintable(result.message));
    const linuxbee::TrackTags t = linuxbee::readTags(result.outputPath).tags;
    QCOMPARE(t.audio.bitsPerSample, bits);
    QCOMPARE(t.audio.sampleRateHz, rate);
    QCOMPARE(t.audio.durationMs, source == s(u"tone.flac") ? 1000 : 500); // GEN: lossless is exact
}

void ConvertTest::lossySource()
{
    // GEN: tone.mp3 has a title, an artist, an album, TRCK 2/5, POPM 255 and a
    // TXXX whose name is not ASCII.
    ConvertJob job;
    job.source = fixture("tone.mp3");
    job.destination = out("from-mp3.flac");
    job.settings = preset("flac");
    const ConvertResult result = linuxbee::convertFile(job);
    QVERIFY2(result.status == ConvertResult::Status::Converted, qPrintable(result.message));
    const linuxbee::TrackTags t = linuxbee::readTags(result.outputPath).tags;
    QCOMPARE(t.audio.bitsPerSample, 16); // C-DEPTH: a lossy source gives 16 bits
    const Fields expected = {
        { s(u"Title"), { s(u"Lossy Title") } },
        { s(u"Artist"), { s(u"Lossy Artist") } },
        { s(u"Album"), { s(u"Lossy Album") } },
        { s(u"Track#"), { s(u"2") } },
        { s(u"Track Count"), { s(u"5") } },
        { s(u"Rating"), { s(u"5") } },
    };
    QCOMPARE(t.fields, expected);
    QVERIFY(bytesOf(result.outputPath).contains("RATING=100")); // BYTES: a Vorbis comment
    // C-TAGS: a tag the writer refuses is left behind and reported, and the
    // conversion still succeeds.
    QCOMPARE(result.warnings.size(), 1);
    QVERIFY2(result.warnings.first().contains(s(u"CL\u00c9")), qPrintable(result.warnings.first()));
    QVERIFY(t.other.isEmpty());
}

void ConvertTest::tagOptions()
{
    ConvertJob job;
    job.source = fixture("tone.flac");
    job.settings = preset("ogg-q5");
    job.destination = out("no-pictures.ogg");
    job.copyPictures = false;
    QCOMPARE(linuxbee::convertFile(job).status, ConvertResult::Status::Converted);
    linuxbee::TrackTags t = linuxbee::readTags(job.destination).tags;
    QVERIFY(t.pictures.isEmpty());
    QCOMPARE(t.fields, toneFields());

    job.destination = out("no-tags.ogg");
    job.copyTags = false;
    job.copyPictures = true; // has no effect without copyTags
    QCOMPARE(linuxbee::convertFile(job).status, ConvertResult::Status::Converted);
    t = linuxbee::readTags(job.destination).tags;
    QVERIFY(t.fields.isEmpty());
    QVERIFY(t.pictures.isEmpty());
    // C-TAGS: nothing of the source's. (ffmpeg's Ogg writer adds an ENCODER
    // comment of its own.)
    QVERIFY(!t.other.contains(s(u"MY KEY")));
    QVERIFY(t.other.keys().removeAll(s(u"ENCODER")) == t.other.size());
}

void ConvertTest::progress()
{
    ConvertJob job;
    job.source = fixture("tone.flac");
    job.destination = out("p.mp3");
    job.settings = preset("mp3-320");
    QList<double> seen;
    const ConvertResult result = linuxbee::convertFile(job, [&seen](double fraction) {
        seen.append(fraction);
        return true;
    });
    QCOMPARE(result.status, ConvertResult::Status::Converted);
    // C-PROGRESS: 0 first, 1 last, never backwards, never outside 0 to 1.
    QVERIFY(seen.size() >= 2);
    QCOMPARE(seen.first(), 0.0);
    QCOMPARE(seen.last(), 1.0);
    for (qsizetype i = 1; i < seen.size(); ++i) {
        QVERIFY(seen.at(i) >= seen.at(i - 1));
        QVERIFY(seen.at(i) >= 0.0 && seen.at(i) <= 1.0);
    }
    QCOMPARE(seen.count(1.0), 1);

    // C-PROGRESS: refusing the first call cancels before ffmpeg is started.
    job.destination = out("never.mp3");
    int calls = 0;
    const ConvertResult cancelled = linuxbee::convertFile(job, [&calls](double) {
        ++calls;
        return false;
    });
    QCOMPARE(cancelled.status, ConvertResult::Status::Cancelled);
    QVERIFY(!cancelled.ok());
    QCOMPARE(calls, 1);
    QCOMPARE(leftovers(), QStringList({ s(u"p.mp3") }));
}

void ConvertTest::ifExists()
{
    ConvertJob job;
    job.source = fixture("tone.flac");
    job.destination = out("song.mp3");
    job.settings = preset("mp3-128");
    { // something is already there
        QFile file(job.destination);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("old");
    }
    // C-EXISTS Skip (the default)
    ConvertResult r = linuxbee::convertFile(job);
    QCOMPARE(r.status, ConvertResult::Status::Skipped);
    QVERIFY(r.ok());
    QCOMPARE(r.outputPath, job.destination);
    QCOMPARE(bytesOf(job.destination), QByteArray("old"));

    // C-EXISTS Rename: P-DUP numbering from docs/spec/paths.md
    job.ifExists = ConvertJob::IfExists::Rename;
    r = linuxbee::convertFile(job);
    QCOMPARE(r.status, ConvertResult::Status::Converted);
    QCOMPARE(r.outputPath, out("song_1.mp3"));
    QCOMPARE(bytesOf(job.destination), QByteArray("old"));
    r = linuxbee::convertFile(job);
    QCOMPARE(r.outputPath, out("song_2.mp3"));
    QCOMPARE(leftovers(), QStringList({ s(u"song.mp3"), s(u"song_1.mp3"), s(u"song_2.mp3") }));

    // C-EXISTS Overwrite: a failed run leaves the old file alone ...
    job.ifExists = ConvertJob::IfExists::Overwrite;
    job.ffmpegPath = script("fail.sh", "echo boom >&2\nexit 1\n");
    r = linuxbee::convertFile(job);
    QCOMPARE(r.status, ConvertResult::Status::Failed);
    QCOMPARE(bytesOf(job.destination), QByteArray("old"));
    // ... and a good run replaces it.
    job.ffmpegPath = QStringLiteral("ffmpeg");
    r = linuxbee::convertFile(job);
    QCOMPARE(r.status, ConvertResult::Status::Converted);
    QCOMPARE(r.outputPath, job.destination);
    QCOMPARE(linuxbee::readTags(job.destination).tags.first(s(u"Title")), s(u"Tone Title"));

    // C-EXISTS UpdateTags: the audio stays, the tags follow the source.
    const QString source = out("source-copy.flac");
    QVERIFY(QFile::copy(fixture("tone.flac"), source));
    QFile::setPermissions(source, QFile::ReadOwner | QFile::WriteOwner);
    linuxbee::TagChanges change;
    change.fields = { { s(u"Title"), { s(u"Changed") } }, { s(u"Genre"), { } } };
    change.other = { { s(u"MY KEY"), { } }, { s(u"NEW KEY"), { s(u"n") } } };
    QCOMPARE(linuxbee::writeTags(source, change).status, linuxbee::WriteResult::Status::Ok);
    const int durationBefore = linuxbee::readTags(job.destination).tags.audio.durationMs;
    job.source = source;
    job.ifExists = ConvertJob::IfExists::UpdateTags;
    job.ffmpegPath = script("never.sh", "echo should not run >&2\nexit 9\n");
    r = linuxbee::convertFile(job);
    QVERIFY2(r.status == ConvertResult::Status::TagsUpdated, qPrintable(r.message));
    QVERIFY(r.ok());
    QCOMPARE(r.outputPath, job.destination);
    const linuxbee::TrackTags t = linuxbee::readTags(job.destination).tags;
    Fields expected = toneFields();
    expected.insert(s(u"Title"), { s(u"Changed") });
    expected.remove(s(u"Genre"));
    QCOMPARE(t.fields, expected);
    QCOMPARE(t.other, Fields({ { s(u"NEW KEY"), { s(u"n") } } }));
    QCOMPARE(t.audio.durationMs, durationBefore);
    QCOMPARE(t.format, s(u"mp3"));
}

void ConvertTest::refusals()
{
    ConvertJob job;
    job.settings = preset("mp3-320");
    job.destination = out("x.mp3");

    // C-STEPS
    job.source = out("no-such.flac");
    QCOMPARE(linuxbee::convertFile(job).status, ConvertResult::Status::SourceNotFound);
    job.source = QStringLiteral(LINUXBEE_TEST_DATA "/tags/notaudio.mp3");
    QCOMPARE(linuxbee::convertFile(job).status, ConvertResult::Status::SourceNotAudio);

    job.source = fixture("tone.flac");
    job.settings.bitrateKbps = 300;
    ConvertResult r = linuxbee::convertFile(job);
    QCOMPARE(r.status, ConvertResult::Status::InvalidJob);
    QVERIFY2(r.message.startsWith(s(u"bitrate:")), qPrintable(r.message));
    job.settings = preset("mp3-320");

    job.destination = QString();
    QCOMPARE(linuxbee::convertFile(job).status, ConvertResult::Status::InvalidJob);

    // The destination is the source, also when Overwrite is asked for.
    const QString copy = out("same.flac");
    QVERIFY(QFile::copy(fixture("tone.flac"), copy));
    const QByteArray before = bytesOf(copy);
    job.source = copy;
    job.destination = m_work + QStringLiteral("/./same.flac");
    job.settings = preset("flac");
    job.ifExists = ConvertJob::IfExists::Overwrite;
    r = linuxbee::convertFile(job);
    QCOMPARE(r.status, ConvertResult::Status::InvalidJob);
    QCOMPARE(bytesOf(copy), before);
    QCOMPARE(leftovers(), QStringList({ s(u"same.flac") }));
    QVERIFY(!r.ok());
}

void ConvertTest::encoderProblems()
{
    ConvertJob job;
    job.source = fixture("tone.flac");
    job.destination = out("x.mp3");
    job.settings = preset("mp3-320");

    // C-ENCODER
    job.ffmpegPath = m_dir.filePath(QStringLiteral("no-such-ffmpeg"));
    ConvertResult r = linuxbee::convertFile(job);
    QCOMPARE(r.status, ConvertResult::Status::EncoderMissing);
    QCOMPARE(leftovers(), QStringList());

    // ffmpeg writes half a file, complains and fails: C-CLEAN, and the last
    // line of its complaint is the message.
    job.ffmpegPath = script("half.sh",
        "for a in \"$@\"; do out=$a; done\n"
        "echo half > \"$out\"\n"
        "echo first line >&2\n"
        "echo the real reason >&2\n"
        "exit 1\n");
    r = linuxbee::convertFile(job);
    QCOMPARE(r.status, ConvertResult::Status::Failed);
    QCOMPARE(r.message, s(u"the real reason"));
    QCOMPARE(leftovers(), QStringList());

    // ffmpeg "succeeds" but what it wrote is not audio: the tags cannot be
    // written, so the job fails and nothing is left.
    job.ffmpegPath = script("liar.sh",
        "for a in \"$@\"; do out=$a; done\n"
        "echo not audio > \"$out\"\n"
        "exit 0\n");
    r = linuxbee::convertFile(job);
    QCOMPARE(r.status, ConvertResult::Status::Failed);
    QVERIFY(!r.message.isEmpty());
    QCOMPARE(leftovers(), QStringList());

    // The real ffmpeg, a folder it cannot write into.
    const QString locked = m_work + QStringLiteral("/locked");
    QVERIFY(QDir().mkpath(locked));
    QFile::setPermissions(locked, QFile::ReadOwner | QFile::ExeOwner);
    job.ffmpegPath = QStringLiteral("ffmpeg");
    job.destination = locked + QStringLiteral("/x.mp3");
    r = linuxbee::convertFile(job);
    QFile::setPermissions(locked, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QCOMPARE(r.status, ConvertResult::Status::Failed);
    QVERIFY(!r.message.isEmpty());
    QCOMPARE(leftovers(), QStringList());
}

void ConvertTest::cancel()
{
    // A stand-in that reports a little progress, leaves half a file and then
    // hangs. C-PROGRESS: answering false stops it promptly; C-CLEAN.
    ConvertJob job;
    job.source = fixture("tone.flac");
    job.destination = out("x.mp3");
    job.settings = preset("mp3-320");
    job.ffmpegPath = script("slow.sh",
        "for a in \"$@\"; do out=$a; done\n"
        "echo half > \"$out\"\n"
        "echo out_time_us=250000\n"
        "echo progress=continue\n"
        "exec sleep 60\n");
    QList<double> seen;
    QElapsedTimer timer;
    timer.start();
    const ConvertResult r = linuxbee::convertFile(job, [&seen](double fraction) {
        seen.append(fraction);
        return fraction == 0.0; // carry on at the start, stop at the first real report
    });
    QCOMPARE(r.status, ConvertResult::Status::Cancelled);
    QVERIFY2(timer.elapsed() < 15000, "cancelling took too long");
    QCOMPARE(seen.size(), 2);
    QCOMPARE(seen.at(1), 0.25); // GEN: 250 000 of 1 000 000 microseconds
    QCOMPARE(leftovers(), QStringList());
}

QTEST_APPLESS_MAIN(ConvertTest)
#include "tst_convert.moc"
