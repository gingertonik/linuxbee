// LOCKED ACCEPTANCE TEST (organise: a file named more than once is planned once; a folder that was
// named is never removed). Written by the reviewer, not the builder. The builder makes these pass
// by changing src/, never by editing this file. A test believed to be wrong is reported, with the
// reason, and left alone.
//
// Expected values are O-PLAN ("An input given more than once") and O-EMPTY ("Floors") in
// docs/spec/organise.md. The faults this holds shut (a probe of 2026-11-19): a file named twice,
// as a folder and the folder above it name it, was planned once for each naming. A Copy made
// x.mp3 and x_1.mp3. A Move moved the file and then reported `the file is gone` and failed. And a
// folder named together with the folder above it was removed when it was left empty.
//
// Every case works in a temporary folder, on copies of the fixtures. No target is replaced, so
// no trash is used; the data folder is this test's own all the same.
#include <QtTest>

#include "organise/organiser.h"

using linuxbee::MoveOutcome;
using linuxbee::OrganiseOptions;
using linuxbee::OrganiseReport;
using linuxbee::PlannedMove;
using Status = linuxbee::PlannedMove::Status;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

QByteArray bytesOf(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArrayLiteral("<cannot open>");
    return file.readAll();
}

// Every file below a folder, as paths from it, sorted.
QStringList filesBelow(const QString &folder)
{
    QStringList found;
    QDirIterator walk(
        folder, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (walk.hasNext())
        found.append(QDir(folder).relativeFilePath(walk.next()));
    found.sort();
    return found;
}

QStringList sourcesOf(const QList<PlannedMove> &plan)
{
    QStringList sources;
    for (const PlannedMove &move : plan)
        sources.append(move.source);
    return sources;
}

QStringList targetsOf(const QList<PlannedMove> &plan)
{
    QStringList targets;
    for (const PlannedMove &move : plan)
        targets.append(move.target);
    return targets;
}

}

class OrganiseTwiceTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void anInputGivenMoreThanOnceIsPlannedOnce();
    void theSamePathWrittenAnotherWayIsTheSameInput();
    void aLinkAndItsFileAreTwoInputs();
    void aCopyMakesOneCopy();
    void aMoveMovesOnceAndNothingFails();
    void aFolderThatWasNamedIsNeverRemoved_data();
    void aFolderThatWasNamedIsNeverRemoved();
    void aFolderBelowTheOneNamedIsRemovedAsBefore();

private:
    OrganiseOptions keepNames() const;
    QTemporaryDir m_dir;
    QString m_work, m_x, m_y, m_out;
    int m_counter = 0;
};

void OrganiseTwiceTest::init()
{
    QVERIFY(m_dir.isValid());
    m_work = QDir::cleanPath(QFileInfo(m_dir.path()).canonicalFilePath())
        + QStringLiteral("/t%1").arg(++m_counter);
    qputenv("XDG_DATA_HOME", QFile::encodeName(m_work + QStringLiteral("/xdg-data")));
    QVERIFY(QDir().mkpath(m_work + QStringLiteral("/in/a")));
    QVERIFY(QDir().mkpath(m_work + QStringLiteral("/out")));
    m_x = m_work + QStringLiteral("/in/a/x.mp3");
    m_y = m_work + QStringLiteral("/in/a/y.flac");
    m_out = m_work + QStringLiteral("/out");
    QVERIFY(QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/basic.mp3"), m_x));
    QVERIFY(QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/basic.flac"), m_y));
}

OrganiseOptions OrganiseTwiceTest::keepNames() const
{
    OrganiseOptions options;
    options.targetRoot = m_out;
    options.useTemplate = false;
    return options;
}

void OrganiseTwiceTest::anInputGivenMoreThanOnceIsPlannedOnce()
{
    // O-PLAN: one entry for each different source, at the place of its first naming. The second
    // naming gets no entry and no numbered name.
    const QList<PlannedMove> plan
        = linuxbee::planOrganise({ m_x, m_y, m_x, m_y, m_x }, keepNames());
    QCOMPARE(sourcesOf(plan), QStringList({ m_x, m_y }));
    QCOMPARE(targetsOf(plan), QStringList({ m_out + s(u"/x.mp3"), m_out + s(u"/y.flac") }));
    for (const PlannedMove &move : plan) {
        QCOMPARE(move.status, Status::Ready);
        QVERIFY(!move.renamed);
        QVERIFY(!move.replaces);
    }
    // The order is that of the first namings.
    QCOMPARE(sourcesOf(linuxbee::planOrganise({ m_y, m_x, m_y }, keepNames())),
        QStringList({ m_y, m_x }));
}

void OrganiseTwiceTest::theSamePathWrittenAnotherWayIsTheSameInput()
{
    // O-PLAN: inputs are compared as clean absolute paths, which is what `source` is.
    const QString other = m_work + s(u"/in/../in/a/./x.mp3");
    const QList<PlannedMove> plan = linuxbee::planOrganise({ other, m_x, m_y }, keepNames());
    QCOMPARE(sourcesOf(plan), QStringList({ m_x, m_y }));
    QCOMPARE(sourcesOf(linuxbee::planOrganise({ m_x, other, m_y }, keepNames())),
        QStringList({ m_x, m_y }));
    // A file that is not there is one input too, however often it is named.
    const QString gone = m_work + s(u"/in/a/gone.mp3");
    const QList<PlannedMove> withGone = linuxbee::planOrganise({ gone, m_x, gone }, keepNames());
    QCOMPARE(sourcesOf(withGone), QStringList({ gone, m_x }));
    QCOMPARE(withGone.value(0).status, Status::NotFound);
}

void OrganiseTwiceTest::aLinkAndItsFileAreTwoInputs()
{
    // By the path, not by what it names: a link beside its file is another source, as before.
    const QString link = m_work + s(u"/in/a/l.mp3");
    QVERIFY(QFile::link(QStringLiteral("x.mp3"), link));
    const QList<PlannedMove> plan = linuxbee::planOrganise({ m_x, link }, keepNames());
    QCOMPARE(sourcesOf(plan), QStringList({ m_x, link }));
}

void OrganiseTwiceTest::aCopyMakesOneCopy()
{
    OrganiseOptions options = keepNames();
    options.mode = OrganiseOptions::Mode::Copy;
    const QByteArray x = bytesOf(m_x);
    const QList<PlannedMove> plan = linuxbee::planOrganise({ m_x, m_y, m_x, m_y }, options);
    const OrganiseReport report = linuxbee::executeOrganise(plan, options);
    QCOMPARE(report.done, 2);
    QCOMPARE(report.failed, 0);
    QCOMPARE(report.skipped, 0);
    QCOMPARE(filesBelow(m_out), QStringList({ s(u"x.mp3"), s(u"y.flac") }));
    QCOMPARE(bytesOf(m_out + s(u"/x.mp3")), x);
    QCOMPARE(filesBelow(m_work + s(u"/in")), QStringList({ s(u"a/x.mp3"), s(u"a/y.flac") }));
}

void OrganiseTwiceTest::aMoveMovesOnceAndNothingFails()
{
    const OrganiseOptions options = keepNames();
    const QByteArray x = bytesOf(m_x);
    const QList<PlannedMove> plan = linuxbee::planOrganise({ m_x, m_y, m_x, m_y }, options);
    const OrganiseReport report = linuxbee::executeOrganise(plan, options);
    QCOMPARE(report.outcomes.size(), 2);
    QCOMPARE(report.done, 2);
    QCOMPARE(report.failed, 0);
    QCOMPARE(filesBelow(m_out), QStringList({ s(u"x.mp3"), s(u"y.flac") }));
    QCOMPARE(bytesOf(m_out + s(u"/x.mp3")), x);
    QCOMPARE(filesBelow(m_work + s(u"/in")), QStringList());
}

void OrganiseTwiceTest::aFolderThatWasNamedIsNeverRemoved_data()
{
    QTest::addColumn<bool>("theOneAboveFirst");
    QTest::newRow("the folder, then the one above it") << false;
    QTest::newRow("the one above it, then the folder") << true;
}

void OrganiseTwiceTest::aFolderThatWasNamedIsNeverRemoved()
{
    // O-EMPTY: a floor is never removed, however empty, and whatever other floor lies above it.
    QFETCH(bool, theOneAboveFirst);
    const QString in = m_work + s(u"/in");
    const QString a = in + s(u"/a");
    OrganiseOptions options = keepNames();
    options.removeEmptyFolders = true;
    options.sourceRoots = theOneAboveFirst ? QStringList({ in, a }) : QStringList({ a, in });
    const QList<PlannedMove> plan = linuxbee::planOrganise({ m_x, m_y }, options);
    const OrganiseReport report = linuxbee::executeOrganise(plan, options);
    QCOMPARE(report.done, 2);
    QCOMPARE(report.removedFolders, QStringList());
    QVERIFY(QFileInfo(a).isDir());
    QVERIFY(QFileInfo(in).isDir());
}

void OrganiseTwiceTest::aFolderBelowTheOneNamedIsRemovedAsBefore()
{
    const QString in = m_work + s(u"/in");
    OrganiseOptions options = keepNames();
    options.removeEmptyFolders = true;
    options.sourceRoots = QStringList({ in });
    const QList<PlannedMove> plan = linuxbee::planOrganise({ m_x, m_y }, options);
    const OrganiseReport report = linuxbee::executeOrganise(plan, options);
    QCOMPARE(report.done, 2);
    QCOMPARE(report.removedFolders, QStringList({ in + s(u"/a") }));
    QVERIFY(QFileInfo(in).isDir());
}

QTEST_GUILESS_MAIN(OrganiseTwiceTest)
#include "tst_organise_twice.moc"
