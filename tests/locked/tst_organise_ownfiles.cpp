// LOCKED ACCEPTANCE TEST (organise: a run never replaces one of its own files). Written by the
// reviewer, not the builder. The builder makes these pass by changing src/, never by editing this
// file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are O-CONFLICT and O-EXECUTE in docs/spec/organise.md. The fault this holds
// shut (the write-path audit, part 2, F1): with "replace existing files" on, a file that a later
// entry of the same run was to move was replaced by an earlier one, and lost.
//
// Every case works in a temporary folder, on copies of basic.flac given a title each. The
// template is <Title> and no target folder is set, so a file's target is its title in its own
// folder. Files are told apart by their bytes (the title is in them). The trash is a folder of
// this test's own.
#include <QtTest>

#include "organise/organiser.h"
#include "tags/tagwriter.h"

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

template <typename List> const typename List::value_type &at(const List &list, qsizetype i)
{
    static const typename List::value_type none { };
    return i < list.size() ? list.at(i) : none;
}

}

class OrganiseOwnFilesTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void aChain_data();
    void aChain();
    void aSwap_data();
    void aSwap();
    void aSecondRunGivesTheNamesWanted();
    void aLaterFileThatStays();
    void aFileThatIsNotOfTheRunIsStillReplaced();
    void withReplaceOffAsBefore();
    void aPlanThatAsksForItIsRefused();
    void theSameFileByAnotherPath();

private:
    QString make(const QString &name, const QString &title);
    QStringList music() const;
    int inTrash() const;
    OrganiseOptions options() const;
    QTemporaryDir m_dir;
    QString m_work;
    int m_counter = 0;
};

void OrganiseOwnFilesTest::init()
{
    QVERIFY(m_dir.isValid());
    m_work = m_dir.filePath(QStringLiteral("t%1").arg(++m_counter));
    QVERIFY(QDir().mkpath(m_work + QStringLiteral("/m")));
    qputenv("XDG_DATA_HOME", QFile::encodeName(m_work + QStringLiteral("/xdg-data")));
}

// m/<name>.flac, a copy of basic.flac whose title is `title`.
QString OrganiseOwnFilesTest::make(const QString &name, const QString &title)
{
    const QString path = m_work + QStringLiteral("/m/") + name + QStringLiteral(".flac");
    QFile::remove(path);
    if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/basic.flac"), path))
        return { };
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
    linuxbee::TagChanges c;
    c.fields.insert(QStringLiteral("Title"), { title });
    if (linuxbee::writeTags(path, c).status != linuxbee::WriteResult::Status::Ok)
        return { };
    return path;
}

// The names in m/, sorted; hidden ones too, so nothing put aside goes unseen.
QStringList OrganiseOwnFilesTest::music() const
{
    return QDir(m_work + QStringLiteral("/m"))
        .entryList(
            QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
}

int OrganiseOwnFilesTest::inTrash() const
{
    return int(QDir(m_work + QStringLiteral("/xdg-data/Trash/files"))
            .entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot)
            .size());
}

OrganiseOptions OrganiseOwnFilesTest::options() const
{
    OrganiseOptions o;
    o.namingTemplate = linuxbee::Template::parse(QStringLiteral("<Title>"));
    o.replaceExisting = true;
    return o;
}

static void modes()
{
    QTest::addColumn<bool>("copy");
    QTest::addColumn<bool>("deleteReplaced");
    QTest::newRow("move, the replaced to the trash") << false << false;
    QTest::newRow("move, the replaced deleted") << false << true;
    QTest::newRow("copy, the replaced to the trash") << true << false;
    QTest::newRow("copy, the replaced deleted") << true << true;
}

void OrganiseOwnFilesTest::aChain_data()
{
    modes();
}

void OrganiseOwnFilesTest::aChain()
{
    // O-CONFLICT: a is to be called b, and b is to be called c. b is a file of the run: a does
    // not replace it, a is numbered.
    QFETCH(bool, copy);
    QFETCH(bool, deleteReplaced);
    const QString a = make(s(u"a"), s(u"b"));
    const QString b = make(s(u"b"), s(u"c"));
    QVERIFY(!a.isEmpty() && !b.isEmpty());
    const QByteArray wasA = bytesOf(a);
    const QByteArray wasB = bytesOf(b);
    QVERIFY(wasA != wasB);
    OrganiseOptions o = options();
    o.mode = copy ? OrganiseOptions::Mode::Copy : OrganiseOptions::Mode::Move;
    o.replaced = deleteReplaced ? OrganiseOptions::Replaced::Delete
                                : OrganiseOptions::Replaced::MoveToTrash;

    const QList<PlannedMove> plan = linuxbee::planOrganise({ a, b }, o);
    QCOMPARE(int(plan.size()), 2);
    QVERIFY(at(plan, 0).status == Status::Ready);
    QCOMPARE(at(plan, 0).target, m_work + s(u"/m/b_1.flac"));
    QVERIFY(at(plan, 0).renamed && !at(plan, 0).replaces);
    QCOMPARE(at(plan, 1).target, m_work + s(u"/m/c.flac"));
    QVERIFY(!at(plan, 1).replaces);

    const OrganiseReport r = linuxbee::executeOrganise(plan, o);
    QCOMPARE(r.done, 2);
    QCOMPARE(r.failed, 0);
    QVERIFY(!at(r.outcomes, 0).replaced && !at(r.outcomes, 1).replaced);
    QCOMPARE(bytesOf(m_work + s(u"/m/b_1.flac")), wasA);
    QCOMPARE(bytesOf(m_work + s(u"/m/c.flac")), wasB);
    if (copy) {
        QCOMPARE(
            music(), QStringList({ s(u"a.flac"), s(u"b.flac"), s(u"b_1.flac"), s(u"c.flac") }));
        QCOMPARE(bytesOf(a), wasA);
        QCOMPARE(bytesOf(b), wasB);
    } else {
        QCOMPARE(music(), QStringList({ s(u"b_1.flac"), s(u"c.flac") }));
    }
    QCOMPARE(inTrash(), 0);
}

void OrganiseOwnFilesTest::aSwap_data()
{
    modes();
}

void OrganiseOwnFilesTest::aSwap()
{
    // a is to be called b and b is to be called a. In a move, a is numbered and b then takes the
    // name a has left. In a copy nothing leaves, so both are numbered.
    QFETCH(bool, copy);
    QFETCH(bool, deleteReplaced);
    const QString a = make(s(u"a"), s(u"b"));
    const QString b = make(s(u"b"), s(u"a"));
    const QByteArray wasA = bytesOf(a);
    const QByteArray wasB = bytesOf(b);
    OrganiseOptions o = options();
    o.mode = copy ? OrganiseOptions::Mode::Copy : OrganiseOptions::Mode::Move;
    o.replaced = deleteReplaced ? OrganiseOptions::Replaced::Delete
                                : OrganiseOptions::Replaced::MoveToTrash;

    const QList<PlannedMove> plan = linuxbee::planOrganise({ a, b }, o);
    QVERIFY(!at(plan, 0).replaces && !at(plan, 1).replaces);
    const OrganiseReport r = linuxbee::executeOrganise(plan, o);
    QCOMPARE(r.done, 2);
    QCOMPARE(r.failed, 0);
    QVERIFY(!at(r.outcomes, 0).replaced && !at(r.outcomes, 1).replaced);
    QCOMPARE(bytesOf(m_work + s(u"/m/b_1.flac")), wasA);
    if (copy) {
        QCOMPARE(
            music(), QStringList({ s(u"a.flac"), s(u"a_1.flac"), s(u"b.flac"), s(u"b_1.flac") }));
        QCOMPARE(bytesOf(a), wasA);
        QCOMPARE(bytesOf(b), wasB);
        QCOMPARE(bytesOf(m_work + s(u"/m/a_1.flac")), wasB);
    } else {
        QCOMPARE(music(), QStringList({ s(u"a.flac"), s(u"b_1.flac") }));
        QCOMPARE(bytesOf(m_work + s(u"/m/a.flac")), wasB);
    }
    QCOMPARE(inTrash(), 0);
}

void OrganiseOwnFilesTest::aSecondRunGivesTheNamesWanted()
{
    // The numbered name is not the end of it: the name wanted is free after the first run.
    const QString a = make(s(u"a"), s(u"b"));
    const QString b = make(s(u"b"), s(u"c"));
    const QByteArray wasA = bytesOf(a);
    const QByteArray wasB = bytesOf(b);
    const OrganiseOptions o = options();
    QCOMPARE(linuxbee::executeOrganise(linuxbee::planOrganise({ a, b }, o), o).done, 2);
    const QStringList now = { m_work + s(u"/m/b_1.flac"), m_work + s(u"/m/c.flac") };
    const QList<PlannedMove> plan = linuxbee::planOrganise(now, o);
    QCOMPARE(at(plan, 0).target, m_work + s(u"/m/b.flac"));
    QVERIFY(!at(plan, 0).replaces);
    QVERIFY(at(plan, 1).status == Status::AlreadyInPlace);
    const OrganiseReport r = linuxbee::executeOrganise(plan, o);
    QCOMPARE(r.done, 1);
    QCOMPARE(music(), QStringList({ s(u"b.flac"), s(u"c.flac") }));
    QCOMPARE(bytesOf(m_work + s(u"/m/b.flac")), wasA);
    QCOMPARE(bytesOf(m_work + s(u"/m/c.flac")), wasB);
    QCOMPARE(inTrash(), 0);
}

void OrganiseOwnFilesTest::aLaterFileThatStays()
{
    // O-CONFLICT: a file of the run is the source of any entry, whatever becomes of that entry.
    // b is where it belongs, and n is no music at all: neither moves, and neither is replaced.
    const QString a = make(s(u"a"), s(u"b"));
    const QString b = make(s(u"b"), s(u"b"));
    const QString x = make(s(u"x"), s(u"n"));
    const QString n = m_work + s(u"/m/n.flac");
    QFile notMusic(n);
    QVERIFY(notMusic.open(QIODevice::WriteOnly));
    notMusic.write("this is not sound");
    notMusic.close();
    const QByteArray wasA = bytesOf(a);
    const QByteArray wasB = bytesOf(b);
    const QByteArray wasX = bytesOf(x);
    const OrganiseOptions o = options();

    const QList<PlannedMove> plan = linuxbee::planOrganise({ a, x, b, n }, o);
    QCOMPARE(at(plan, 0).target, m_work + s(u"/m/b_1.flac"));
    QVERIFY(!at(plan, 0).replaces);
    QCOMPARE(at(plan, 1).target, m_work + s(u"/m/n_1.flac"));
    QVERIFY(!at(plan, 1).replaces);
    QVERIFY(at(plan, 2).status == Status::AlreadyInPlace);
    QVERIFY(at(plan, 3).status == Status::NotAudio);

    const OrganiseReport r = linuxbee::executeOrganise(plan, o);
    QCOMPARE(r.done, 2);
    QCOMPARE(r.failed, 0);
    QCOMPARE(bytesOf(b), wasB);
    QCOMPARE(bytesOf(n), QByteArray("this is not sound"));
    QCOMPARE(bytesOf(m_work + s(u"/m/b_1.flac")), wasA);
    QCOMPARE(bytesOf(m_work + s(u"/m/n_1.flac")), wasX);
    QCOMPARE(music(), QStringList({ s(u"b.flac"), s(u"b_1.flac"), s(u"n.flac"), s(u"n_1.flac") }));
    QCOMPARE(inTrash(), 0);
}

void OrganiseOwnFilesTest::aFileThatIsNotOfTheRunIsStillReplaced()
{
    // The other side: what "replace existing files" is for still works. b is on the disk and is
    // not handed to the run.
    const QString a = make(s(u"a"), s(u"b"));
    const QString b = make(s(u"b"), s(u"something else"));
    const QByteArray wasA = bytesOf(a);
    const OrganiseOptions o = options();
    const QList<PlannedMove> plan = linuxbee::planOrganise({ a }, o);
    QCOMPARE(at(plan, 0).target, b);
    QVERIFY(at(plan, 0).replaces && !at(plan, 0).renamed);
    const OrganiseReport r = linuxbee::executeOrganise(plan, o);
    QCOMPARE(r.done, 1);
    QVERIFY(at(r.outcomes, 0).replaced);
    QCOMPARE(bytesOf(b), wasA);
    QCOMPARE(music(), QStringList({ s(u"b.flac") }));
    QCOMPARE(inTrash(), 1);
}

void OrganiseOwnFilesTest::withReplaceOffAsBefore()
{
    const QString a = make(s(u"a"), s(u"b"));
    const QString b = make(s(u"b"), s(u"c"));
    const QByteArray wasA = bytesOf(a);
    const QByteArray wasB = bytesOf(b);
    OrganiseOptions o = options();
    o.replaceExisting = false;
    const QList<PlannedMove> plan = linuxbee::planOrganise({ a, b }, o);
    QCOMPARE(at(plan, 0).target, m_work + s(u"/m/b_1.flac"));
    const OrganiseReport r = linuxbee::executeOrganise(plan, o);
    QCOMPARE(r.done, 2);
    QCOMPARE(bytesOf(m_work + s(u"/m/b_1.flac")), wasA);
    QCOMPARE(bytesOf(m_work + s(u"/m/c.flac")), wasB);
}

void OrganiseOwnFilesTest::aPlanThatAsksForItIsRefused()
{
    // O-EXECUTE: the one who carries a plan out does not believe it. This plan is made by hand
    // and is the plan the fault used to make: a replaces b, and b goes on to c.
    for (const bool deleteReplaced : { false, true }) {
        const QString a = make(s(u"a"), s(u"b"));
        const QString b = make(s(u"b"), s(u"c"));
        QFile::remove(m_work + s(u"/m/c.flac"));
        const QByteArray wasA = bytesOf(a);
        const QByteArray wasB = bytesOf(b);
        OrganiseOptions o = options();
        o.replaced = deleteReplaced ? OrganiseOptions::Replaced::Delete
                                    : OrganiseOptions::Replaced::MoveToTrash;
        PlannedMove first;
        first.status = Status::Ready;
        first.source = a;
        first.target = b;
        first.replaces = true;
        PlannedMove second;
        second.status = Status::Ready;
        second.source = b;
        second.target = m_work + s(u"/m/c.flac");

        const OrganiseReport r = linuxbee::executeOrganise({ first, second }, o);
        QVERIFY(at(r.outcomes, 0).status == MoveOutcome::Status::Failed);
        QVERIFY2(at(r.outcomes, 0).message.contains(s(u"another file of this run")),
            qPrintable(at(r.outcomes, 0).message));
        QVERIFY(!at(r.outcomes, 0).replaced);
        QVERIFY(at(r.outcomes, 1).status == MoveOutcome::Status::Done);
        QCOMPARE(bytesOf(a), wasA); // a is where it was
        QCOMPARE(bytesOf(m_work + s(u"/m/c.flac")), wasB); // and b is c, itself
        QCOMPARE(music(), QStringList({ s(u"a.flac"), s(u"c.flac") }));
        QCOMPARE(inTrash(), 0);
    }
}

void OrganiseOwnFilesTest::theSameFileByAnotherPath()
{
    // A file of the run is a file, not a spelling of its path. l is a link to the folder m: a is
    // named through the link and b through the folder. Whether the plan numbers a or the run
    // refuses it is not ruled; that nothing is replaced is.
    QVERIFY(QFile::link(m_work + s(u"/m"), m_work + s(u"/l")));
    make(s(u"a"), s(u"b"));
    const QString b = make(s(u"b"), s(u"c"));
    const QByteArray wasA = bytesOf(m_work + s(u"/m/a.flac"));
    const QByteArray wasB = bytesOf(b);
    const OrganiseOptions o = options();
    const QList<PlannedMove> plan = linuxbee::planOrganise({ m_work + s(u"/l/a.flac"), b }, o);
    const OrganiseReport r = linuxbee::executeOrganise(plan, o);
    QVERIFY(!at(r.outcomes, 0).replaced && !at(r.outcomes, 1).replaced);
    QCOMPARE(inTrash(), 0);
    QList<QByteArray> found;
    for (const QString &name : music())
        found.append(bytesOf(m_work + s(u"/m/") + name));
    QCOMPARE(int(found.size()), 2);
    QVERIFY2(found.contains(wasA), "the first file's bytes are nowhere");
    QVERIFY2(found.contains(wasB), "the second file's bytes are nowhere");
}

QTEST_MAIN(OrganiseOwnFilesTest)
#include "tst_organise_ownfiles.moc"
