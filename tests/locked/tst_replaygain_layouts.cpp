// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values: docs/spec/replay-gain.md, N-MEASURE and N-FINITE as
// rewritten after the review of plan 17. The numbers are what ffmpeg
// n9.0.2 prints for each fixture with nothing but
// "[0:a:0]ebur128=peak=true" (see tests/data/sound/README.md).
#include <QtTest>

#include <cmath>

#include "sound/replaygain.h"

using linuxbee::Loudness;

namespace {

QString fixture(const char *name)
{
    return QStringLiteral(LINUXBEE_TEST_DATA "/sound/") + QLatin1String(name);
}

bool near(double a, double b, double by = 0.25)
{
    return std::isfinite(a) && std::fabs(a - b) <= by;
}

QByteArray shown(const Loudness &l)
{
    return l.ok ? QStringLiteral("I %1 LUFS, peak %2 dBFS").arg(l.integratedLufs).arg(l.truePeakDbfs).toUtf8()
                : (QStringLiteral("not ok: ") + l.error).toUtf8();
}

QString summary(const char *loudness, const char *peak)
{
    return QStringLiteral("[Parsed_ebur128_0 @ 0x55] Summary:\n\n  Integrated loudness:\n    I:   %1 LUFS\n"
                          "    Threshold: -31.8 LUFS\n\n  Loudness range:\n    LRA:   0.0 LU\n\n  True peak:\n    Peak:  %2 dBFS\n")
        .arg(QLatin1String(loudness), QLatin1String(peak));
}

}

class ReplayGainLayoutsTest : public QObject {
    Q_OBJECT
private slots:
    void oneFileAsItIs();
    void anAlbumOfOneLayout();
    void anAlbumOfSeveralLayouts();
    void notANumber();
};

void ReplayGainLayoutsTest::oneFileAsItIs()
{
    // One channel: it used to be spread over two first, which put the peak 3 dB low.
    const Loudness mono = linuxbee::measureLoudness({ fixture("mono.flac") });
    QVERIFY2(mono.ok && near(mono.integratedLufs, -27.8) && near(mono.truePeakDbfs, -24.1), shown(mono).constData());
    // Six channels: they used to be mixed down to two first, 3 dB louder and with a higher peak.
    const Loudness six = linuxbee::measureLoudness({ fixture("surround.flac") });
    QVERIFY2(six.ok && near(six.integratedLufs, -20.1) && near(six.truePeakDbfs, -24.1), shown(six).constData());
    // Two channels, at two sample rates: as before.
    const Loudness tone = linuxbee::measureLoudness({ fixture("tone.flac") });
    QVERIFY2(tone.ok && near(tone.integratedLufs, -21.8) && near(tone.truePeakDbfs, -21.1), shown(tone).constData());
    const Loudness hires = linuxbee::measureLoudness({ fixture("hires.flac") });
    QVERIFY2(hires.ok, shown(hires).constData());
}

void ReplayGainLayoutsTest::anAlbumOfOneLayout()
{
    // Joined as they are. The peak is the largest of the files' own.
    for (const QStringList &files : { QStringList { fixture("mono.flac"), fixture("quietmono.flac") },
             QStringList { fixture("quietmono.flac"), fixture("mono.flac") } }) {
        const Loudness album = linuxbee::measureLoudness(files);
        QVERIFY2(album.ok && near(album.integratedLufs, -30.5) && near(album.truePeakDbfs, -24.1), shown(album).constData());
    }
    const Loudness sixes = linuxbee::measureLoudness({ fixture("surround.flac"), fixture("surround.flac") });
    QVERIFY2(sixes.ok && near(sixes.integratedLufs, -20.1) && near(sixes.truePeakDbfs, -24.1), shown(sixes).constData());
    // Two rates, one layout: the peak is still the files' own, not that of a resampled join.
    const Loudness tone = linuxbee::measureLoudness({ fixture("tone.flac") });
    const Loudness hires = linuxbee::measureLoudness({ fixture("hires.flac") });
    const Loudness both = linuxbee::measureLoudness({ fixture("hires.flac"), fixture("tone.flac") });
    QVERIFY(tone.ok && hires.ok && both.ok);
    QCOMPARE(both.truePeakDbfs, std::max(tone.truePeakDbfs, hires.truePeakDbfs));
}

void ReplayGainLayoutsTest::anAlbumOfSeveralLayouts()
{
    // Brought to the layout with the most channels, which keeps each file's
    // loudness; the order given does not matter.
    for (const QStringList &files : { QStringList { fixture("mono.flac"), fixture("surround.flac") },
             QStringList { fixture("surround.flac"), fixture("mono.flac") } }) {
        const Loudness album = linuxbee::measureLoudness(files);
        QVERIFY2(album.ok && near(album.integratedLufs, -22.5) && near(album.truePeakDbfs, -24.1), shown(album).constData());
    }
    for (const QStringList &files : { QStringList { fixture("mono.flac"), fixture("tone.flac") },
             QStringList { fixture("tone.flac"), fixture("mono.flac") } }) {
        const Loudness album = linuxbee::measureLoudness(files);
        QVERIFY2(album.ok && near(album.integratedLufs, -24.9) && near(album.truePeakDbfs, -21.1), shown(album).constData());
    }
    // The peak is the files' own even when the join had to change the loudest of them:
    // spread over two channels the mono file would peak at -27.1.
    for (const QStringList &files : { QStringList { fixture("mono.flac"), fixture("quietstereo.flac") },
             QStringList { fixture("quietstereo.flac"), fixture("mono.flac") } }) {
        const Loudness album = linuxbee::measureLoudness(files);
        QVERIFY2(album.ok && near(album.integratedLufs, -30.3) && near(album.truePeakDbfs, -24.1), shown(album).constData());
    }
    // Three layouts and two rates at once.
    const Loudness all = linuxbee::measureLoudness(
        { fixture("hires.flac"), fixture("mono.flac"), fixture("surround.flac"), fixture("tone.flac") });
    QVERIFY2(all.ok && near(all.truePeakDbfs, -21.1) && all.integratedLufs > -27.8 && all.integratedLufs < -20.1,
        shown(all).constData());
}

void ReplayGainLayoutsTest::notANumber()
{
    // N-FINITE. A sane summary first, so that the text below is known to be read at all.
    const Loudness sane = linuxbee::parseLoudnessSummary(summary("-21.8", "-21.1"));
    QVERIFY2(sane.ok && near(sane.integratedLufs, -21.8, 0.001) && near(sane.truePeakDbfs, -21.1, 0.001), shown(sane).constData());
    for (const char *bad : { "nan", "NaN", "-nan", "+nan", "inf", "+inf", "Infinity", "1e999" }) {
        const Loudness l = linuxbee::parseLoudnessSummary(summary(bad, "-21.1"));
        QVERIFY2(!l.ok && !l.error.isEmpty(), bad);
    }
    for (const char *bad : { "nan", "-nan", "inf", "+inf", "1e999" }) {
        const Loudness l = linuxbee::parseLoudnessSummary(summary("-21.8", bad));
        QVERIFY2(!l.ok && !l.error.isEmpty(), bad);
    }
    // A peak of -inf is still digital silence beside a real loudness.
    const Loudness silentPeak = linuxbee::parseLoudnessSummary(summary("-21.8", "-inf"));
    QVERIFY(silentPeak.ok && near(silentPeak.truePeakDbfs, -120.0, 0.001));

    // Nothing that is not a number is ever written, whoever made the measurement.
    Loudness good;
    good.ok = true;
    good.integratedLufs = -21.8;
    good.truePeakDbfs = -21.1;
    QCOMPARE(linuxbee::gainTagChanges(good, good).other.size(), 4);
    for (const double odd : { std::nan(""), HUGE_VAL, -HUGE_VAL }) {
        Loudness a = good;
        a.integratedLufs = odd;
        Loudness b = good;
        b.truePeakDbfs = odd;
        QCOMPARE(linuxbee::gainTagChanges(a, std::nullopt).other.size(), 0);
        QCOMPARE(linuxbee::gainTagChanges(b, std::nullopt).other.size(), 0);
        QCOMPARE(linuxbee::gainTagChanges(good, a).other.size(), 2); // the track's two only
        QCOMPARE(linuxbee::gainTagChanges(b, good).other.size(), 2); // the album's two only
    }

    // And none is read back from a tag.
    for (const char *bad : { "nan", "nan dB", "inf dB", "-inf dB", "+inf", "infinity", "1e999 dB" }) {
        linuxbee::TrackTags tags;
        const QString text = QLatin1String(bad);
        tags.other.insert(QStringLiteral("REPLAYGAIN_TRACK_GAIN"), QStringList { text });
        tags.other.insert(QStringLiteral("REPLAYGAIN_TRACK_PEAK"), QStringList { text.section(QLatin1Char(' '), 0, 0) });
        const linuxbee::GainValues v = linuxbee::gainFromTags(tags);
        QVERIFY2(!v.trackGainDb && !v.trackPeak, bad);
        const double factor = linuxbee::gainFactor(v);
        QCOMPARE(factor, 1.0);
    }
}

QTEST_GUILESS_MAIN(ReplayGainLayoutsTest)
#include "tst_replaygain_layouts.moc"
