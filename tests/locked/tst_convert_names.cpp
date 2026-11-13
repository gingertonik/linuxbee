// Locked test (review of plan 08). The expected values come from the spec and
// the fixtures (GEN), not from running the code.
//
// docs/spec/convert.md, C-STEPS and C-ARGS: the source is converted to the
// destination, whatever the two are called. A relative name that starts with
// "-" or with "word:" is a file name: ffmpeg must not read it as an option or
// as a protocol (pipe:, concat:, http:, ...).
#include <QtTest>

#include "convert/converter.h"
#include "tags/tagreader.h"

using linuxbee::ConvertJob;
using linuxbee::ConvertResult;

namespace {

QString fixture(const char *name)
{
    return QStringLiteral(LINUXBEE_TEST_DATA "/convert/") + QLatin1String(name);
}

linuxbee::EncoderSettings preset(const char *id)
{
    const std::optional<linuxbee::EncoderPreset> found
        = linuxbee::encoderPreset(QLatin1String(id));
    return found ? found->settings : linuxbee::EncoderSettings();
}

}

class ConvertNamesTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void leadingDash();
    void protocolLookalike();

private:
    QTemporaryDir m_dir;
    QString m_previous;
};

// Every case works with names relative to a fresh current folder.
void ConvertNamesTest::init()
{
    QVERIFY(m_dir.isValid());
    m_previous = QDir::currentPath();
    QVERIFY(QDir::setCurrent(m_dir.path()));
}

void ConvertNamesTest::cleanup()
{
    QDir::setCurrent(m_previous);
}

// A source and a destination named "-y...": converted, nothing else written.
void ConvertNamesTest::leadingDash()
{
    QVERIFY(QFile::copy(fixture("tone.flac"), QStringLiteral("-y.flac")));
    ConvertJob job;
    job.source = QStringLiteral("-y.flac");
    job.destination = QStringLiteral("-y.mp3");
    job.settings = preset("mp3-320");
    const ConvertResult r = linuxbee::convertFile(job);
    QVERIFY2(r.status == ConvertResult::Status::Converted, qPrintable(r.message));
    QCOMPARE(linuxbee::readTags(QStringLiteral("-y.mp3")).status,
        linuxbee::ReadResult::Status::Ok);
    QStringList names = QDir::current().entryList(QDir::Files);
    names.sort();
    QCOMPARE(names, QStringList({ QStringLiteral("-y.flac"), QStringLiteral("-y.mp3") }));
}

// A file called "concat:a.flac" is that file, not ffmpeg's concat of a.flac.
// GEN: tone.flac is 44100 Hz, hires.flac is 96000 Hz; FLAC keeps the rate.
void ConvertNamesTest::protocolLookalike()
{
    QVERIFY(QFile::copy(fixture("hires.flac"), QStringLiteral("a.flac")));
    QVERIFY(QFile::copy(fixture("tone.flac"), QStringLiteral("concat:a.flac")));
    ConvertJob job;
    job.source = QStringLiteral("concat:a.flac");
    job.destination = QStringLiteral("out.flac");
    job.settings = preset("flac");
    ConvertResult r = linuxbee::convertFile(job);
    QVERIFY2(r.status == ConvertResult::Status::Converted, qPrintable(r.message));
    QCOMPARE(linuxbee::readTags(QStringLiteral("out.flac")).tags.audio.sampleRateHz, 44100);

    // And a destination called "pipe:1.flac" is a file of that name.
    job.source = QStringLiteral("a.flac");
    job.destination = QStringLiteral("pipe:1.flac");
    r = linuxbee::convertFile(job);
    QVERIFY2(r.status == ConvertResult::Status::Converted, qPrintable(r.message));
    QCOMPARE(linuxbee::readTags(QStringLiteral("pipe:1.flac")).tags.audio.sampleRateHz, 96000);
}

QTEST_APPLESS_MAIN(ConvertNamesTest)
#include "tst_convert_names.moc"
