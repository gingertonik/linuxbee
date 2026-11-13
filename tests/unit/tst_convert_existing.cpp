// Builder's test (tests/unit). Expected values come from the rules named in
// each comment, not from running the code.
#include <QtTest>

#include "convert/converter.h"
#include "tags/tagreader.h"

using linuxbee::ConvertJob;
using linuxbee::ConvertResult;

class ConvertExistingTest : public QObject {
    Q_OBJECT
private slots:
    void overwriteKeepsTheNeighbourNamedLikeTheSetAsideFile();
};

// C-CLEAN and C-EXISTS: an overwrite may replace the destination, and nothing
// else. A file that already has the name the old file is set aside under is
// the user's, so it is neither removed nor overwritten.
void ConvertExistingTest::overwriteKeepsTheNeighbourNamedLikeTheSetAsideFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString destination = dir.filePath(QStringLiteral("song.mp3"));
    const QString neighbour = dir.filePath(QStringLiteral("song.mp3.replaced"));
    {
        QFile file(destination);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("old");
    }
    {
        QFile file(neighbour);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("keep me");
    }

    ConvertJob job;
    job.source = QStringLiteral(LINUXBEE_TEST_DATA "/convert/tone.flac");
    job.destination = destination;
    job.settings = linuxbee::encoderPresets().first().settings; // C-PRESETS: mp3-320
    job.ifExists = ConvertJob::IfExists::Overwrite;

    const ConvertResult result = linuxbee::convertFile(job);
    QCOMPARE(result.status, ConvertResult::Status::Converted);

    // The neighbour is untouched, byte for byte.
    QFile after(neighbour);
    QVERIFY(after.open(QIODevice::ReadOnly));
    QCOMPARE(after.readAll(), QByteArray("keep me"));

    // The destination now holds the new file, which reads as audio.
    QCOMPARE(linuxbee::readTags(destination).status, linuxbee::ReadResult::Status::Ok);
}

QTEST_APPLESS_MAIN(ConvertExistingTest)
#include "tst_convert_existing.moc"
