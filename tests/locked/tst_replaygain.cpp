// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/replay-gain.md (N-x) worked by hand:
// a factor is 10^(dB/20). RG2 is the ReplayGain 2.0 specification, R128 is
// RFC 7845 section 5.2.1 (Opus gain tags). The two measurements of the
// fixtures are what ffmpeg n9.0.2 reports, allowed a quarter of a decibel.
#include <QtTest>

#include <cmath>

#include "sound/replaygain.h"

using linuxbee::GainOptions;
using linuxbee::GainValues;
using linuxbee::Loudness;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

QString fixture(const char *name)
{
    return QStringLiteral(LINUXBEE_TEST_DATA "/sound/") + QLatin1String(name);
}

linuxbee::TrackTags tagged(const QMap<QString, QStringList> &other)
{
    linuxbee::TrackTags t;
    t.other = other;
    return t;
}

bool near(double a, double b, double tolerance = 0.00005)
{
    return std::fabs(a - b) <= tolerance;
}

// The summary ffmpeg prints, with the numbers given.
QString summary(const char *integrated, const char *peak)
{
    return QStringLiteral("[Parsed_ebur128_0 @ 0x7f5428003b80] Summary:\n\n  Integrated loudness:\n"
                          "    I:         %1 LUFS\n    Threshold: -31.8 LUFS\n\n  Loudness range:\n"
                          "    LRA:         0.0 LU\n    Threshold:   0.0 LUFS\n    LRA low:     0.0 LUFS\n"
                          "    LRA high:    0.0 LUFS\n\n  True peak:\n    Peak:      %2 dBFS\n"
                          "[out#0/null @ 0x5623cc0f2a00] video:0KiB audio:172KiB\n")
        .arg(QLatin1String(integrated), QLatin1String(peak));
}

}

class ReplayGainTest : public QObject {
    Q_OBJECT
private slots:
    void fromTags_data();
    void fromTags();
    void fromTagsWhole();
    void factor();
    void parse();
    void changes();
    void measure();
    void measureFailures();
};

void ReplayGainTest::fromTags_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<bool>("isGain");
    QTest::addColumn<bool>("valid");
    QTest::addColumn<double>("expected");
    auto row = [](const char *name, const char16_t *text, bool isGain, bool valid, double expected = 0) {
        QTest::newRow(name) << QString::fromUtf16(text) << isGain << valid << expected;
    };
    // N-TAGS (RG2: "-6.50 dB"; readers are asked to be forgiving)
    row("gain", u"-6.50 dB", true, true, -6.5);
    row("gain-plus", u"+3.80 dB", true, true, 3.8);
    row("gain-no-sign", u"3.8 dB", true, true, 3.8);
    row("gain-no-unit", u"-6.5", true, true, -6.5);
    row("gain-no-space", u"-6.50dB", true, true, -6.5);
    row("gain-case-and-spaces", u"  -6.50  DB ", true, true, -6.5);
    row("gain-integer", u"-7 dB", true, true, -7.0);
    row("gain-zero", u"0.00 dB", true, true, 0.0);
    row("gain-empty", u"", true, false);
    row("gain-words", u"loud", true, false);
    row("gain-comma", u"-6,50 dB", true, false);
    row("gain-only-unit", u"dB", true, false);
    row("gain-two-numbers", u"-6.5 -7 dB", true, false);
    row("gain-exponent", u"1e2 dB", true, false);
    row("peak", u"0.988553", false, true, 0.988553);
    row("peak-over-one", u"1.204312", false, true, 1.204312);
    row("peak-spaces", u" 0.5 ", false, true, 0.5);
    row("peak-zero", u"0.000000", false, false); // a peak of nothing says nothing
    row("peak-negative", u"-0.5", false, false);
    row("peak-with-unit", u"0.5 dB", false, false);
    row("peak-words", u"n/a", false, false);
}

void ReplayGainTest::fromTags()
{
    QFETCH(QString, text);
    QFETCH(bool, isGain);
    QFETCH(bool, valid);
    QFETCH(double, expected);
    const GainValues v = linuxbee::gainFromTags(tagged({ { isGain ? s(u"REPLAYGAIN_TRACK_GAIN") : s(u"REPLAYGAIN_TRACK_PEAK"), { text } },
        { isGain ? s(u"REPLAYGAIN_ALBUM_GAIN") : s(u"REPLAYGAIN_ALBUM_PEAK"), { text } } }));
    const std::optional<double> track = isGain ? v.trackGainDb : v.trackPeak;
    const std::optional<double> album = isGain ? v.albumGainDb : v.albumPeak;
    QCOMPARE(track.has_value(), valid);
    QCOMPARE(album.has_value(), valid);
    if (valid) {
        QVERIFY(near(*track, expected));
        QVERIFY(near(*album, expected));
    }
    QVERIFY(!(isGain ? v.trackPeak : v.trackGainDb).has_value()); // the other kind was not given
}

void ReplayGainTest::fromTagsWhole()
{
    // N-TAGS: nothing there, nothing found.
    GainValues v = linuxbee::gainFromTags(linuxbee::TrackTags());
    QVERIFY(!v.trackGainDb && !v.trackPeak && !v.albumGainDb && !v.albumPeak);
    // Keys in any case; the first value counts; catalogue fields are not looked at.
    linuxbee::TrackTags t = tagged({ { s(u"replaygain_track_gain"), { s(u"-1.00 dB"), s(u"-9.00 dB") } },
        { s(u"ReplayGain_Album_Peak"), { s(u"0.25") } } });
    t.fields.insert(s(u"Comment"), { s(u"REPLAYGAIN_ALBUM_GAIN=-3 dB") });
    v = linuxbee::gainFromTags(t);
    QVERIFY(v.trackGainDb && near(*v.trackGainDb, -1.0));
    QVERIFY(v.albumPeak && near(*v.albumPeak, 0.25));
    QVERIFY(!v.albumGainDb && !v.trackPeak);
    // N-R128: Opus files carry R128_*_GAIN, a whole number of 1/256 dB relative
    // to -23 LUFS. ReplayGain's reference is 5 dB louder: gain = value / 256 + 5.
    v = linuxbee::gainFromTags(tagged({ { s(u"R128_TRACK_GAIN"), { s(u"-1234") } }, { s(u"R128_ALBUM_GAIN"), { s(u"256") } } }));
    QVERIFY(v.trackGainDb && near(*v.trackGainDb, -1234.0 / 256.0 + 5.0)); // 0.1796875
    QVERIFY(v.albumGainDb && near(*v.albumGainDb, 6.0));
    QVERIFY(!v.trackPeak && !v.albumPeak);
    v = linuxbee::gainFromTags(tagged({ { s(u"R128_TRACK_GAIN"), { s(u"0") } } }));
    QVERIFY(v.trackGainDb && near(*v.trackGainDb, 5.0));
    // Not a whole number in the 16-bit range: ignored.
    v = linuxbee::gainFromTags(tagged({ { s(u"R128_TRACK_GAIN"), { s(u"1.5") } }, { s(u"R128_ALBUM_GAIN"), { s(u"40000") } } }));
    QVERIFY(!v.trackGainDb && !v.albumGainDb);
    // A REPLAYGAIN tag wins over an R128 tag for the same thing.
    v = linuxbee::gainFromTags(tagged({ { s(u"R128_TRACK_GAIN"), { s(u"256") } }, { s(u"REPLAYGAIN_TRACK_GAIN"), { s(u"-2 dB") } } }));
    QVERIFY(near(*v.trackGainDb, -2.0));
    // ... unless it cannot be read.
    v = linuxbee::gainFromTags(tagged({ { s(u"R128_TRACK_GAIN"), { s(u"256") } }, { s(u"REPLAYGAIN_TRACK_GAIN"), { s(u"?") } } }));
    QVERIFY(near(*v.trackGainDb, 6.0));
}

void ReplayGainTest::factor()
{
    // N-APPLY. 10^(-6.5/20) = 0.473151, 10^(3.8/20) = 1.548817, 10^(6/20) = 1.995262,
    // 10^(-4.5/20) = 0.595662, 10^(-3/20) = 0.707946
    GainValues v;
    v.trackGainDb = -6.5;
    v.albumGainDb = 3.8;
    GainOptions o;
    QVERIFY(o.mode == GainOptions::Mode::Track && o.preampDb == 0.0 && o.untaggedDb == 0.0 && o.preventClipping);
    QVERIFY(near(linuxbee::gainFactor(v), 0.473151));
    o.mode = GainOptions::Mode::Album;
    QVERIFY(near(linuxbee::gainFactor(v, o), 1.548817));
    o.mode = GainOptions::Mode::Off;
    QCOMPARE(linuxbee::gainFactor(v, o), 1.0);
    // The pre-amp is added to the gain.
    o.mode = GainOptions::Mode::Track;
    o.preampDb = 2.0;
    QVERIFY(near(linuxbee::gainFactor(v, o), 0.595662));
    // The other kind stands in for a missing one.
    GainValues trackOnly;
    trackOnly.trackGainDb = -6.5;
    o = GainOptions();
    o.mode = GainOptions::Mode::Album;
    QVERIFY(near(linuxbee::gainFactor(trackOnly, o), 0.473151));
    GainValues albumOnly;
    albumOnly.albumGainDb = 3.8;
    QVERIFY(near(linuxbee::gainFactor(albumOnly), 1.548817));
    // No gain at all: the untagged level, and the pre-amp is not added.
    o = GainOptions();
    QCOMPARE(linuxbee::gainFactor(GainValues(), o), 1.0);
    o.untaggedDb = -3.0;
    o.preampDb = 6.0;
    QVERIFY(near(linuxbee::gainFactor(GainValues(), o), 0.707946));
    o.mode = GainOptions::Mode::Off;
    QCOMPARE(linuxbee::gainFactor(GainValues(), o), 1.0);

    // Clipping: the factor is held to 1 / peak. The peak used is the one that goes with the gain used.
    GainValues loud;
    loud.trackGainDb = 6.0;
    loud.trackPeak = 0.9;
    loud.albumGainDb = 6.0;
    loud.albumPeak = 0.25;
    o = GainOptions();
    QVERIFY(near(linuxbee::gainFactor(loud, o), 1.0 / 0.9)); // 1.995 * 0.9 would be 1.796
    o.mode = GainOptions::Mode::Album;
    QVERIFY(near(linuxbee::gainFactor(loud, o), 1.995262)); // 1.995 * 0.25 is under 1
    o.preventClipping = false;
    o.mode = GainOptions::Mode::Track;
    QVERIFY(near(linuxbee::gainFactor(loud, o), 1.995262));
    // No peak known: nothing to hold it to.
    GainValues noPeak;
    noPeak.trackGainDb = 6.0;
    QVERIFY(near(linuxbee::gainFactor(noPeak), 1.995262));
    // A stand-in gain brings its own peak.
    GainValues standIn;
    standIn.albumGainDb = 6.0;
    standIn.albumPeak = 0.8;
    standIn.trackPeak = 0.1; // not used: there is no track gain
    QVERIFY(near(linuxbee::gainFactor(standIn), 1.25));
    // A gain that turns the volume down never needs holding.
    GainValues quiet;
    quiet.trackGainDb = -6.5;
    quiet.trackPeak = 1.5;
    QVERIFY(near(linuxbee::gainFactor(quiet), 0.473151));
}

void ReplayGainTest::parse()
{
    // N-PARSE, N-REFERENCE: gain = -18 - I (RG2); peak = 10^(dBFS/20)
    Loudness l = linuxbee::parseLoudnessSummary(summary("-21.8", "-21.1"));
    QVERIFY2(l.ok, qPrintable(l.error));
    QVERIFY(near(l.integratedLufs, -21.8));
    QVERIFY(near(l.truePeakDbfs, -21.1));
    QVERIFY(near(l.gainDb(), 3.8));
    QVERIFY(near(l.peak(), 0.088105, 0.0000005));
    l = linuxbee::parseLoudnessSummary(summary("-9.3", "0.4"));
    QVERIFY(l.ok);
    QVERIFY(near(l.gainDb(), -8.7));
    QVERIFY(near(l.peak(), 1.047129, 0.0000005));
    // Only the last summary counts, and what comes before it is ignored (it may hold "I:" too).
    l = linuxbee::parseLoudnessSummary(s(u"t: 0.4 TARGET:-23 LUFS M: -20.0 S:-120.7 I: -20.0 LUFS LRA: 0.0 LU\n")
        + summary("-30.0", "-3.0") + summary("-12.5", "-1.0"));
    QVERIFY(l.ok);
    QVERIFY(near(l.integratedLufs, -12.5));
    // Too quiet or too short for a loudness: not ok, with a reason.
    for (const char *silent : { "-70.0", "-inf", "-120.7" }) {
        l = linuxbee::parseLoudnessSummary(summary(silent, "-inf"));
        QVERIFY(!l.ok);
        QVERIFY(!l.error.isEmpty());
    }
    // A loudness with a peak of -inf cannot happen, but must not break: the peak is then tiny.
    l = linuxbee::parseLoudnessSummary(summary("-20.0", "-inf"));
    QVERIFY(l.ok);
    QVERIFY(l.peak() < 0.00001);
    // Not a summary.
    for (const QString &text : { QString(), s(u"ffmpeg: no such file"), s(u"Summary:\n nothing here\n"),
             s(u"Summary:\n    I:         -21.8 LUFS\n") }) {
        l = linuxbee::parseLoudnessSummary(text);
        QVERIFY(!l.ok);
        QVERIFY(!l.error.isEmpty());
    }
}

void ReplayGainTest::changes()
{
    // N-WRITE: "+3.80 dB" / "-8.70 dB" with a sign and two decimals; the peak with six.
    const Loudness track = linuxbee::parseLoudnessSummary(summary("-21.8", "-21.1"));
    const Loudness album = linuxbee::parseLoudnessSummary(summary("-9.3", "0.4"));
    linuxbee::TagChanges c = linuxbee::gainTagChanges(track, album);
    QCOMPARE(c.other,
        (QMap<QString, QStringList> { { s(u"REPLAYGAIN_TRACK_GAIN"), { s(u"+3.80 dB") } },
            { s(u"REPLAYGAIN_TRACK_PEAK"), { s(u"0.088105") } },
            { s(u"REPLAYGAIN_ALBUM_GAIN"), { s(u"-8.70 dB") } },
            { s(u"REPLAYGAIN_ALBUM_PEAK"), { s(u"1.047129") } } }));
    QVERIFY(c.fields.isEmpty() && !c.pictures.has_value());
    c = linuxbee::gainTagChanges(track, std::nullopt);
    QCOMPARE(c.other.keys(), QStringList({ s(u"REPLAYGAIN_TRACK_GAIN"), s(u"REPLAYGAIN_TRACK_PEAK") })); // album tags are left as they are
    c = linuxbee::gainTagChanges(std::nullopt, album);
    QCOMPARE(c.other.keys(), QStringList({ s(u"REPLAYGAIN_ALBUM_GAIN"), s(u"REPLAYGAIN_ALBUM_PEAK") }));
    QVERIFY(linuxbee::gainTagChanges(std::nullopt, std::nullopt).isEmpty());
    QVERIFY(linuxbee::gainTagChanges(Loudness(), Loudness()).isEmpty()); // a failed measurement writes nothing
    // Exactly zero is "+0.00 dB", and never "-0.00 dB".
    c = linuxbee::gainTagChanges(linuxbee::parseLoudnessSummary(summary("-18.0", "-6.0")), std::nullopt);
    QCOMPARE(c.other.value(s(u"REPLAYGAIN_TRACK_GAIN")), QStringList({ s(u"+0.00 dB") }));
    // What is written reads back as the same values.
    const GainValues back = linuxbee::gainFromTags(tagged(linuxbee::gainTagChanges(track, album).other));
    QVERIFY(near(*back.trackGainDb, 3.8) && near(*back.albumGainDb, -8.7));
    QVERIFY(near(*back.trackPeak, 0.088105) && near(*back.albumPeak, 1.047129));

    // Removing: every REPLAYGAIN_* key and the two R128 keys the track has, in the case it has them.
    linuxbee::TrackTags t = tagged({ { s(u"REPLAYGAIN_TRACK_GAIN"), { s(u"-1 dB") } },
        { s(u"replaygain_reference_loudness"), { s(u"89 dB") } }, { s(u"R128_ALBUM_GAIN"), { s(u"0") } },
        { s(u"CATALOGNUMBER"), { s(u"X1") } }, { s(u"R128_SOMETHING"), { s(u"1") } } });
    c = linuxbee::removeGainTagChanges(t);
    QCOMPARE(c.other,
        (QMap<QString, QStringList> { { s(u"REPLAYGAIN_TRACK_GAIN"), { } },
            { s(u"replaygain_reference_loudness"), { } }, { s(u"R128_ALBUM_GAIN"), { } } }));
    QVERIFY(c.fields.isEmpty() && !c.pictures.has_value());
    QVERIFY(linuxbee::removeGainTagChanges(linuxbee::TrackTags()).isEmpty());
}

void ReplayGainTest::measure()
{
    // N-MEASURE. ffmpeg n9.0.2 reports -21.8 LUFS and -21.1 dBFS for tone.flac.
    const Loudness tone = linuxbee::measureLoudness({ fixture("tone.flac") });
    QVERIFY2(tone.ok, qPrintable(tone.error));
    QVERIFY2(near(tone.integratedLufs, -21.8, 0.25), qPrintable(QString::number(tone.integratedLufs)));
    QVERIFY2(near(tone.truePeakDbfs, -21.1, 0.25), qPrintable(QString::number(tone.truePeakDbfs)));
    // A file at another sample rate and bit depth measures too.
    const Loudness hires = linuxbee::measureLoudness({ fixture("hires.flac") });
    QVERIFY2(hires.ok, qPrintable(hires.error));
    QVERIFY(hires.integratedLufs < -10.0 && hires.integratedLufs > -30.0);
    // An album is its tracks heard as one: between the quietest and the loudest of them,
    // and the same whichever order they are given in.
    const Loudness album = linuxbee::measureLoudness({ fixture("tone.flac"), fixture("hires.flac") });
    QVERIFY2(album.ok, qPrintable(album.error));
    const double low = std::min(tone.integratedLufs, hires.integratedLufs) - 0.15;
    const double high = std::max(tone.integratedLufs, hires.integratedLufs) + 0.15;
    QVERIFY(album.integratedLufs >= low && album.integratedLufs <= high);
    QVERIFY(near(album.truePeakDbfs, std::max(tone.truePeakDbfs, hires.truePeakDbfs), 0.25));
    const Loudness reversed = linuxbee::measureLoudness({ fixture("hires.flac"), fixture("tone.flac") });
    QVERIFY(reversed.ok);
    QVERIFY(near(reversed.integratedLufs, album.integratedLufs, 0.15));
    // The same file twice is as loud as once.
    const Loudness twice = linuxbee::measureLoudness({ fixture("tone.flac"), fixture("tone.flac") });
    QVERIFY(twice.ok);
    QVERIFY(near(twice.integratedLufs, tone.integratedLufs, 0.15));
}

void ReplayGainTest::measureFailures()
{
    // Each of these is "not ok" with a reason, promptly, and without an exception or a hang.
    QTemporaryDir dir;
    const QStringList none;
    for (const QStringList &files : { none, QStringList { fixture("no-such-file.flac") },
             QStringList { fixture("notaudio.mp3") }, QStringList { dir.path() },
             QStringList { fixture("tone.flac"), fixture("no-such-file.flac") } }) {
        const Loudness l = linuxbee::measureLoudness(files);
        QVERIFY(!l.ok);
        QVERIFY(!l.error.isEmpty());
    }
    const Loudness noProgram = linuxbee::measureLoudness({ fixture("tone.flac") }, s(u"/nonexistent/ffmpeg"));
    QVERIFY(!noProgram.ok);
    QVERIFY(!noProgram.error.isEmpty());
    // A path that looks like an option or a protocol is still only a file name.
    QFile::copy(fixture("tone.flac"), dir.path() + s(u"/-i odd:name.flac"));
    QDir::setCurrent(dir.path());
    const Loudness odd = linuxbee::measureLoudness({ s(u"-i odd:name.flac") });
    QDir::setCurrent(QDir::rootPath());
    QVERIFY2(odd.ok, qPrintable(odd.error));
}

QTEST_GUILESS_MAIN(ReplayGainTest)
#include "tst_replaygain.moc"
