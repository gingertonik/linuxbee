// LOCKED ACCEPTANCE TEST (review of plan 08b). Written by the reviewer.
// The builder makes these pass by changing src/, never by editing this file.
//
// B-DEST: "No job may write to a path that is one of the sources, or the
// destination of an earlier job." A job writes two paths: its destination
// and, while ffmpeg runs, its part file (docs/spec/convert.md C-CLEAN: the
// destination's name with ".part" before its extension, removed first).
// So a destination is not free when its part file is a source or an earlier
// job's destination, nor when it is an earlier job's part file.
#include <QCryptographicHash>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "convert/convertbatch.h"

using namespace linuxbee;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

QStringList destinations(const QStringList &sources)
{
    QStringList out;
    for (const ConvertJob &j : planBatch(sources, BatchOptions()))
        out.append(j.destination);
    return out;
}

QByteArray hashOf(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return { };
    return QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256);
}

}

class ConvertBatchReviewTest : public QObject {
    Q_OBJECT
private slots:
    void partFileIsASource();
    void partFileIsAnEarlierDestination();
    void destinationIsAnEarlierPartFile();
    void otherNamesUnchanged();
    void realRunKeepsTheSource();
};

void ConvertBatchReviewTest::partFileIsASource()
{
    // a.mp3 would be written through a.part.mp3, which is the second source.
    QCOMPARE(destinations({ s(u"/m/a.flac"), s(u"/m/a.part.mp3") }),
        QStringList({ s(u"/m/a_1.mp3"), s(u"/m/a.part_1.mp3") }));
}

void ConvertBatchReviewTest::partFileIsAnEarlierDestination()
{
    // The first job's result is a.part.mp3; the second must not use it as its part file.
    QCOMPARE(destinations({ s(u"/m/a.part.flac"), s(u"/m/a.flac") }),
        QStringList({ s(u"/m/a.part.mp3"), s(u"/m/a_1.mp3") }));
}

void ConvertBatchReviewTest::destinationIsAnEarlierPartFile()
{
    // The first job writes a.part.mp3 while it runs; the second must not finish there.
    QCOMPARE(destinations({ s(u"/m/a.flac"), s(u"/m/a.part.flac") }),
        QStringList({ s(u"/m/a.mp3"), s(u"/m/a.part_1.mp3") }));
}

void ConvertBatchReviewTest::otherNamesUnchanged()
{
    QCOMPARE(destinations({ s(u"/m/a.flac"), s(u"/m/b.flac"), s(u"/m/a.ogg") }),
        QStringList({ s(u"/m/a.mp3"), s(u"/m/b.mp3"), s(u"/m/a_1.mp3") }));
    QCOMPARE(destinations({ s(u"/m/part.flac"), s(u"/m/a.parts.mp3") }),
        QStringList({ s(u"/m/part.mp3"), s(u"/m/a.parts_1.mp3") }));
}

void ConvertBatchReviewTest::realRunKeepsTheSource()
{
    // A selected file named like another job's part file survives the batch.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString flac = dir.filePath(s(u"a.flac"));
    const QString other = dir.filePath(s(u"a.part.mp3"));
    QVERIFY(QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/convert/tone.flac"), flac));
    {
        QFile f(other);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QVERIFY(f.write("a file the user selected") > 0);
    }
    const QByteArray before = hashOf(other);
    BatchOptions o;
    o.settings = encoderPreset(s(u"mp3-320")).value().settings;
    ConvertBatch b;
    b.setMaxParallel(1);
    QSignalSpy finished(&b, &ConvertBatch::finished);
    QVERIFY(b.start(planBatch({ flac, other }, o)));
    QVERIFY(finished.wait(60000));
    QVERIFY2(b.results().value(0).status == ConvertResult::Status::Converted,
        qPrintable(b.results().value(0).message));
    QVERIFY(QFile::exists(other));
    QCOMPARE(hashOf(other), before);
    QVERIFY(QFileInfo(b.results().value(0).outputPath).size() > 1000);
}

QTEST_GUILESS_MAIN(ConvertBatchReviewTest)
#include "tst_convertbatch_review.moc"
