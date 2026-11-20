// LOCKED ACCEPTANCE TEST (Organise: three rules that no test held).
// Written by the reviewer, not the builder. The builder makes these pass by changing src/, never
// by editing this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Rules: docs/spec/organise.md, O-PLAN (inputs are told apart by their whole path), O-EXECUTE (a
// link is judged against its file where the file now is) and O-ACROSS (a link does not cross).
// Each was found unheld by a review that planted a fault and saw every test pass. The program
// is right in all three already: these cases pass before any change.
//
// The cases across a disk need /dev/shm to be writable and another file system than the
// temporary folder, as tst_organise_across.cpp does. Without it they fail, and say so.
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <sys/stat.h>
#include <unistd.h>

#include "organise/organiser.h"

using linuxbee::MoveOutcome;
using linuxbee::OrganiseOptions;
using linuxbee::OrganiseReport;
using linuxbee::PlannedMove;

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

// Every file and link below a folder, as paths from it, sorted. Folders reached through a
// link are not walked.
QStringList namesBelow(const QString &folder)
{
    QStringList found;
    QDirIterator walk(
        folder, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (walk.hasNext())
        found.append(QDir(folder).relativeFilePath(walk.next()));
    found.sort();
    return found;
}

quint64 deviceOf(const QString &path)
{
    struct stat st { };
    return ::stat(QFile::encodeName(path).constData(), &st) == 0 ? quint64(st.st_dev) : 0;
}

// The text of a link, or "<no link>".
QString textOf(const QString &path)
{
    QByteArray text(4096, '\0');
    const ssize_t n
        = ::readlink(QFile::encodeName(path).constData(), text.data(), size_t(text.size()));
    return n < 0 ? QStringLiteral("<no link>") : QFile::decodeName(text.left(int(n)));
}

bool makeLink(const char *text, const QString &at)
{
    return ::symlink(text, QFile::encodeName(at).constData()) == 0;
}

PlannedMove ready(const QString &source, const QString &target)
{
    PlannedMove move;
    move.source = source;
    move.target = target;
    move.status = PlannedMove::Status::Ready;
    return move;
}

QString outcomeOf(const OrganiseReport &report, int index)
{
    const MoveOutcome outcome = report.outcomes.value(index);
    switch (outcome.status) {
    case MoveOutcome::Status::Done:
        return QStringLiteral("done");
    case MoveOutcome::Status::Skipped:
        return QStringLiteral("skipped: ") + outcome.message;
    case MoveOutcome::Status::Failed:
        return QStringLiteral("failed: ") + outcome.message;
    }
    return { };
}

struct Two {
    QTemporaryDir here; // where tests keep their files
    QTemporaryDir there { QStringLiteral("/dev/shm/linuxbee-test-XXXXXX") };
    bool ok() const
    {
        return here.isValid() && there.isValid() && deviceOf(here.path()) != deviceOf(there.path());
    }
};

#define NEED_TWO(t)                                                                                \
    do {                                                                                           \
        if (!(t).ok())                                                                             \
            QFAIL("this test needs /dev/shm to be writable and another file system than the "      \
                  "temporary folder");                                                             \
    } while (false)

}

class OrganiseLocksTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void twoFilesOfOneNameAreTwoInputs();
    void twoFilesOfOneNameAreBothCopied();
    void aLinkIsJudgedAgainstItsFileOnTheOtherDisk_data();
    void aLinkIsJudgedAgainstItsFileOnTheOtherDisk();
    void aLinkDoesNotCrossToAnotherDisk();

private:
    QTemporaryDir m_dir;
    QString m_work;
    int m_counter = 0;
};

void OrganiseLocksTest::init()
{
    QVERIFY(m_dir.isValid());
    m_work = QDir::cleanPath(QFileInfo(m_dir.path()).canonicalFilePath())
        + QStringLiteral("/t%1").arg(++m_counter);
    qputenv("XDG_DATA_HOME", QFile::encodeName(m_work + QStringLiteral("/xdg-data")));
    QVERIFY(QDir().mkpath(m_work + QStringLiteral("/in/a")));
    QVERIFY(QDir().mkpath(m_work + QStringLiteral("/in/b")));
    QVERIFY(QDir().mkpath(m_work + QStringLiteral("/out")));
}

void OrganiseLocksTest::twoFilesOfOneNameAreTwoInputs()
{
    // O-PLAN: inputs are told apart by their whole path, not by their name. Two files of one
    // name in two folders are two inputs, each named twice here; the second gets a numbered name.
    const QString one = m_work + s(u"/in/a/x.mp3");
    const QString two = m_work + s(u"/in/b/x.mp3");
    QVERIFY(QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/basic.mp3"), one));
    QVERIFY(QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/v23.mp3"), two));
    OrganiseOptions options;
    options.targetRoot = m_work + s(u"/out");
    options.useTemplate = false;
    const QList<PlannedMove> plan = linuxbee::planOrganise({ one, two, one, two }, options);
    QCOMPARE(plan.size(), 2);
    QCOMPARE(plan.value(0).source, one);
    QCOMPARE(plan.value(1).source, two);
    QCOMPARE(plan.value(0).target, m_work + s(u"/out/x.mp3"));
    QCOMPARE(plan.value(1).target, m_work + s(u"/out/x_1.mp3"));
    QVERIFY(!plan.value(0).renamed);
    QVERIFY(plan.value(1).renamed);
}

void OrganiseLocksTest::twoFilesOfOneNameAreBothCopied()
{
    const QString one = m_work + s(u"/in/a/x.mp3");
    const QString two = m_work + s(u"/in/b/x.mp3");
    QVERIFY(QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/basic.mp3"), one));
    QVERIFY(QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/v23.mp3"), two));
    QVERIFY(bytesOf(one) != bytesOf(two));
    OrganiseOptions options;
    options.targetRoot = m_work + s(u"/out");
    options.useTemplate = false;
    options.mode = OrganiseOptions::Mode::Copy;
    const QList<PlannedMove> plan = linuxbee::planOrganise({ one, two, one, two }, options);
    const OrganiseReport report = linuxbee::executeOrganise(plan, options);
    QCOMPARE(report.done, 2);
    QCOMPARE(report.failed, 0);
    QCOMPARE(namesBelow(m_work + s(u"/out")), QStringList({ s(u"x.mp3"), s(u"x_1.mp3") }));
    QCOMPARE(bytesOf(m_work + s(u"/out/x.mp3")), bytesOf(one));
    QCOMPARE(bytesOf(m_work + s(u"/out/x_1.mp3")), bytesOf(two));
}

void OrganiseLocksTest::aLinkIsJudgedAgainstItsFileOnTheOtherDisk_data()
{
    QTest::addColumn<bool>("fileFirst");
    QTest::newRow("the link, then its file") << false;
    QTest::newRow("the file, then its link") << true;
}

void OrganiseLocksTest::aLinkIsJudgedAgainstItsFileOnTheOtherDisk()
{
    // O-EXECUTE: a file moved to another file system is a new file there, and its link is
    // judged against that one. The link `a.mp3` says `t/x.mp3`. It stays on this disk and
    // goes to out2, where `t` is a link to a folder on the other disk; the file goes into
    // that folder. From out2 the text leads to the file where it now is.
    QFETCH(bool, fileFirst);
    Two t;
    NEED_TWO(t);
    const QString in = m_work + s(u"/in");
    QVERIFY(QDir().mkpath(in + s(u"/t")));
    const QString file = in + s(u"/t/x.mp3");
    const QString link = in + s(u"/a.mp3");
    QVERIFY(QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/basic.mp3"), file));
    const QByteArray content = bytesOf(file);
    QVERIFY(makeLink("t/x.mp3", link));
    const QString far = QFileInfo(t.there.path()).canonicalFilePath() + s(u"/c");
    QVERIFY(QDir().mkpath(far));
    const QString out2 = m_work + s(u"/out2");
    QVERIFY(QDir().mkpath(out2));
    QVERIFY(makeLink(QFile::encodeName(far).constData(), out2 + s(u"/t")));
    QList<PlannedMove> plan = { ready(link, out2 + s(u"/a.mp3")), ready(file, far + s(u"/x.mp3")) };
    if (fileFirst)
        plan.swapItemsAt(0, 1);
    const OrganiseReport report = linuxbee::executeOrganise(plan, OrganiseOptions());
    QCOMPARE(outcomeOf(report, 0), QStringLiteral("done"));
    QCOMPARE(outcomeOf(report, 1), QStringLiteral("done"));
    QCOMPARE(report.failed, 0);
    QCOMPARE(textOf(out2 + s(u"/a.mp3")), QStringLiteral("t/x.mp3"));
    QCOMPARE(bytesOf(out2 + s(u"/a.mp3")), content); // read through the link
    QCOMPARE(bytesOf(far + s(u"/x.mp3")), content);
    QCOMPARE(namesBelow(in), QStringList());
}

void OrganiseLocksTest::aLinkDoesNotCrossToAnotherDisk()
{
    // O-ACROSS: a link is never taken to another file system. It stays, still leading to its
    // file, and the words are those of a move across, not those of a link that would break.
    Two t;
    NEED_TWO(t);
    const QString file = m_work + s(u"/in/x.mp3");
    const QString link = m_work + s(u"/in/a.mp3");
    QVERIFY(QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/basic.mp3"), file));
    const QByteArray content = bytesOf(file);
    QVERIFY(makeLink("x.mp3", link));
    const QString far = QFileInfo(t.there.path()).canonicalFilePath();
    const OrganiseReport report
        = linuxbee::executeOrganise({ ready(link, far + s(u"/a.mp3")) }, OrganiseOptions());
    QCOMPARE(outcomeOf(report, 0), QStringLiteral("failed: could not copy the file"));
    QCOMPARE(textOf(link), QStringLiteral("x.mp3"));
    QCOMPARE(bytesOf(link), content);
    QCOMPARE(namesBelow(far), QStringList());
}

QTEST_GUILESS_MAIN(OrganiseLocksTest)
#include "tst_organise_locks.moc"
