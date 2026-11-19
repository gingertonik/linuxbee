// LOCKED ACCEPTANCE TEST (organise: a link and its own file in one run). Written by the reviewer,
// not the builder. The builder makes these pass by changing src/, never by editing this file. A
// test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are O-EXECUTE ("A link and its own file in one run") in docs/spec/organise.md.
// The faults this holds shut (the review of "a link that would break is not moved", 2026-11-19):
// a link planned before its own file, both moved by one run to one folder, was refused, because
// its file was not there yet; the file then moved and the link was left behind, leading nowhere.
// Before that rule both had moved and the link had worked. And a link planned after its own file
// had always failed with `the file is gone`: the link was there, its file had just been moved.
//
// The plans are made by hand (executeOrganise takes a plan wherever it came from), so that a
// link and its file can be given different folders. All files are copies in a temporary folder:
//   in/x.mp3              the music
//   in/a.mp3, in/z.mp3    links to it, with the text "x.mp3"
#include <QtTest>

#include <sys/stat.h>
#include <unistd.h>

#include "organise/organiser.h"

using linuxbee::MoveOutcome;
using linuxbee::OrganiseOptions;
using linuxbee::OrganiseReport;
using linuxbee::PlannedMove;

namespace {

const QString kBroken = QStringLiteral("the link would not lead to its file from there");

QByteArray bytesOf(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArrayLiteral("<cannot open>");
    return file.readAll();
}

// What the name itself is: "link TEXT", "file", "folder" or "nothing".
QString whatIs(const QString &path)
{
    struct stat st { };
    const QByteArray name = QFile::encodeName(path);
    if (::lstat(name.constData(), &st) != 0)
        return QStringLiteral("nothing");
    if (S_ISLNK(st.st_mode)) {
        char text[4096];
        const ssize_t n = ::readlink(name.constData(), text, sizeof text);
        return QStringLiteral("link ") + QFile::decodeName(QByteArray(text, n > 0 ? int(n) : 0));
    }
    return S_ISDIR(st.st_mode) ? QStringLiteral("folder") : QStringLiteral("file");
}

QStringList l(std::initializer_list<const char *> names)
{
    QStringList list;
    for (const char *name : names)
        list.append(QLatin1String(name));
    return list;
}

PlannedMove ready(const QString &source, const QString &target)
{
    PlannedMove move;
    move.status = PlannedMove::Status::Ready;
    move.source = source;
    move.target = target;
    return move;
}

QString wordsOf(const OrganiseReport &report)
{
    QStringList words;
    for (const MoveOutcome &outcome : report.outcomes) {
        const QString name = QFileInfo(outcome.source).fileName();
        switch (outcome.status) {
        case MoveOutcome::Status::Done:
            words.append(name + QStringLiteral(" done"));
            break;
        case MoveOutcome::Status::Skipped:
            words.append(name + QStringLiteral(" skipped: ") + outcome.message);
            break;
        case MoveOutcome::Status::Failed:
            words.append(name + QStringLiteral(" failed: ") + outcome.message);
            break;
        }
    }
    return words.join(QStringLiteral(" | "));
}

}

class OrganiseLinkWithFileTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void bothToOneFolder_data();
    void bothToOneFolder();
    void theLinkWouldNotLeadToWhereItsFileWent_data();
    void theLinkWouldNotLeadToWhereItsFileWent();
    void theFileIsNotMovedAfterAll_data();
    void theFileIsNotMovedAfterAll();
    void stoppedBetweenTheLinkAndItsFile();
    void theProgressIsAskedOnceForEachEntryInOrder();
    void aCopyIsAsBefore();

private:
    QString in(const char *name) const
    {
        return m_work + QStringLiteral("/in/") + QLatin1String(name);
    }
    QString out(const char *name) const
    {
        return m_work + QStringLiteral("/out/") + QLatin1String(name);
    }
    bool link(const char *name) const
    {
        return ::symlink("x.mp3", QFile::encodeName(in(name)).constData()) == 0;
    }
    QList<PlannedMove> planOf(const QStringList &order, const QString &linkFolder) const;
    QTemporaryDir m_dir;
    QString m_work;
    QByteArray m_music;
    int m_counter = 0;
};

void OrganiseLinkWithFileTest::init()
{
    QVERIFY(m_dir.isValid());
    m_work = QDir::cleanPath(QFileInfo(m_dir.path()).canonicalFilePath())
        + QStringLiteral("/t%1").arg(++m_counter);
    qputenv("XDG_DATA_HOME", QFile::encodeName(m_work + QStringLiteral("/xdg-data")));
    QVERIFY(QDir().mkpath(m_work + QStringLiteral("/in")));
    QVERIFY(QDir().mkpath(m_work + QStringLiteral("/out")));
    QVERIFY(QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/basic.mp3"), in("x.mp3")));
    m_music = bytesOf(in("x.mp3"));
    QVERIFY(link("a.mp3"));
    QVERIFY(link("z.mp3"));
}

// The entries named in `order` ("a", "x", "z"), the file to out/ and the links to
// out/`linkFolder` (empty: out/ itself).
QList<PlannedMove> OrganiseLinkWithFileTest::planOf(
    const QStringList &order, const QString &linkFolder) const
{
    QList<PlannedMove> plan;
    for (const QString &name : order) {
        const QString file = name + QStringLiteral(".mp3");
        const QString folder = name == QLatin1String("x") || linkFolder.isEmpty()
            ? m_work + QStringLiteral("/out/")
            : m_work + QStringLiteral("/out/") + linkFolder + QLatin1Char('/');
        plan.append(ready(m_work + QStringLiteral("/in/") + file, folder + file));
    }
    return plan;
}

void OrganiseLinkWithFileTest::bothToOneFolder_data()
{
    QTest::addColumn<QStringList>("order");
    QTest::newRow("the link, then its file") << l({ "a", "x" });
    QTest::newRow("the file, then its link") << l({ "x", "z" });
    QTest::newRow("a link, the file, a link") << l({ "a", "x", "z" });
    QTest::newRow("two links, then the file") << l({ "a", "z", "x" });
    QTest::newRow("the file, then two links") << l({ "x", "a", "z" });
}

void OrganiseLinkWithFileTest::bothToOneFolder()
{
    // The text "x.mp3" leads to the file from the folder they both go to. Whatever the order of
    // the plan, all are moved, nothing fails, and each link leads to the music where it now is.
    // The outcomes are in the plan's order.
    QFETCH(QStringList, order);
    const QList<PlannedMove> plan = planOf(order, QString());
    const OrganiseReport report = linuxbee::executeOrganise(plan, OrganiseOptions());
    QStringList want;
    for (const QString &name : order)
        want.append(name + QStringLiteral(".mp3 done"));
    QCOMPARE(wordsOf(report), want.join(QStringLiteral(" | ")));
    QCOMPARE(report.done, int(order.size()));
    QCOMPARE(report.failed, 0);
    QCOMPARE(whatIs(out("x.mp3")), QStringLiteral("file"));
    QCOMPARE(bytesOf(out("x.mp3")), m_music);
    for (const char *name : { "a.mp3", "z.mp3" }) {
        if (!order.contains(QString(QLatin1String(name)).left(1)))
            continue;
        QCOMPARE(whatIs(out(name)), QStringLiteral("link x.mp3"));
        QCOMPARE(bytesOf(out(name)), m_music);
        QCOMPARE(whatIs(in(name)), QStringLiteral("nothing"));
    }
    QCOMPARE(whatIs(in("x.mp3")), QStringLiteral("nothing"));
}

void OrganiseLinkWithFileTest::theLinkWouldNotLeadToWhereItsFileWent_data()
{
    QTest::addColumn<QStringList>("order");
    QTest::addColumn<QString>("words");
    QTest::newRow("the link, then its file")
        << l({ "a", "x" })
        << (QStringLiteral("a.mp3 failed: ") + kBroken + QStringLiteral(" | x.mp3 done"));
    QTest::newRow("the file, then its link")
        << l({ "x", "a" }) << (QStringLiteral("x.mp3 done | a.mp3 failed: ") + kBroken);
}

void OrganiseLinkWithFileTest::theLinkWouldNotLeadToWhereItsFileWent()
{
    // The file goes to out/, the link to out/deep/. From there "x.mp3" leads to nothing. The
    // file is moved; the link is not, and the run says why, in either order. The link is judged
    // by where its file has gone, not by where it was.
    QFETCH(QStringList, order);
    QFETCH(QString, words);
    const OrganiseReport report
        = linuxbee::executeOrganise(planOf(order, QStringLiteral("deep")), OrganiseOptions());
    QCOMPARE(wordsOf(report), words);
    QCOMPARE(report.done, 1);
    QCOMPARE(report.failed, 1);
    QCOMPARE(bytesOf(out("x.mp3")), m_music);
    QCOMPARE(whatIs(in("a.mp3")), QStringLiteral("link x.mp3"));
    QCOMPARE(whatIs(out("deep/a.mp3")), QStringLiteral("nothing"));
}

void OrganiseLinkWithFileTest::theFileIsNotMovedAfterAll_data()
{
    QTest::addColumn<QStringList>("order");
    QTest::newRow("the link, then its file") << l({ "a", "x" });
    QTest::newRow("the file, then its link") << l({ "x", "a" });
}

void OrganiseLinkWithFileTest::theFileIsNotMovedAfterAll()
{
    // Something is at the file's target, so its move fails. The link is judged by the fact,
    // not by the plan: its file is where it was, from out/ the text leads to another file, and
    // the link stays. It still leads to the music.
    QFETCH(QStringList, order);
    QVERIFY(QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/basic.flac"), out("x.mp3")));
    const QByteArray other = bytesOf(out("x.mp3"));
    const OrganiseReport report
        = linuxbee::executeOrganise(planOf(order, QString()), OrganiseOptions());
    QCOMPARE(report.done, 0);
    QCOMPARE(report.failed, 2);
    QCOMPARE(report.outcomes.size(), 2);
    const MoveOutcome &ofTheLink = report.outcomes.at(order.indexOf(QStringLiteral("a")));
    QCOMPARE(ofTheLink.message, kBroken);
    QCOMPARE(whatIs(in("a.mp3")), QStringLiteral("link x.mp3"));
    QCOMPARE(bytesOf(in("a.mp3")), m_music);
    QCOMPARE(whatIs(out("a.mp3")), QStringLiteral("nothing"));
    QCOMPARE(bytesOf(out("x.mp3")), other);
}

void OrganiseLinkWithFileTest::stoppedBetweenTheLinkAndItsFile()
{
    // The run is stopped when it comes to the file. The link was come to before that, so it has
    // its outcome, and the outcomes are the plan's first entries in order: its file was not
    // moved, the text would not lead to it from out/, the link stays.
    const OrganiseReport report = linuxbee::executeOrganise(planOf(l({ "a", "x" }), QString()),
        OrganiseOptions(), [](int index, int) { return index < 1; });
    QVERIFY(report.cancelled);
    QCOMPARE(wordsOf(report), QStringLiteral("a.mp3 failed: ") + kBroken);
    QCOMPARE(report.done, 0);
    QCOMPARE(report.failed, 1);
    QCOMPARE(whatIs(in("a.mp3")), QStringLiteral("link x.mp3"));
    QCOMPARE(bytesOf(in("x.mp3")), m_music);
    QCOMPARE(whatIs(out("x.mp3")), QStringLiteral("nothing"));
}

void OrganiseLinkWithFileTest::theProgressIsAskedOnceForEachEntryInOrder()
{
    // O-EXECUTE: before each entry, its index and the count. A link that waits for its file is
    // not asked about a second time.
    QList<int> asked;
    const OrganiseReport report = linuxbee::executeOrganise(
        planOf(l({ "a", "z", "x" }), QString()), OrganiseOptions(), [&asked](int index, int count) {
            asked.append(index * 10 + count);
            return true;
        });
    QCOMPARE(asked, QList<int>({ 3, 13, 23 }));
    QCOMPARE(report.done, 3);
}

void OrganiseLinkWithFileTest::aCopyIsAsBefore()
{
    // A Copy reads what a link leads to and moves nothing: three files of the music's bytes.
    OrganiseOptions options;
    options.mode = OrganiseOptions::Mode::Copy;
    const OrganiseReport report
        = linuxbee::executeOrganise(planOf(l({ "a", "x", "z" }), QString()), options);
    QCOMPARE(report.done, 3);
    for (const char *name : { "a.mp3", "x.mp3", "z.mp3" }) {
        QCOMPARE(whatIs(out(name)), QStringLiteral("file"));
        QCOMPARE(bytesOf(out(name)), m_music);
    }
    QCOMPARE(whatIs(in("a.mp3")), QStringLiteral("link x.mp3"));
}

QTEST_GUILESS_MAIN(OrganiseLinkWithFileTest)
#include "tst_organise_linkwithfile.moc"
