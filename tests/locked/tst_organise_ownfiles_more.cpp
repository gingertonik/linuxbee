// LOCKED ACCEPTANCE TEST (organise: a run never replaces one of its own files; four more cases).
// Written by the reviewer, not the builder. The builder makes these pass by changing src/, never
// by editing this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Rules: O-CONFLICT and O-EXECUTE in docs/spec/organise.md. The review of that rule ran a
// chain of three, a ring of three and two swaps by hand and found them right, and found that
// no test held the one exception of the second lock: a file at the target that is the entry's
// own source, by another name, is not "another file of this run". The program is right in
// all four already: these cases pass before any change.
//
// As tst_organise_ownfiles.cpp: a temporary folder, copies of basic.flac given a title each,
// the template <Title>, files told apart by their bytes, a trash of this test's own.
#include <QtTest>

#include <unistd.h>

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

class OrganiseOwnFilesMoreTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void aChainOfThree();
    void aRingOfThree();
    void twoSwaps();
    void itsOwnSourceByAnotherNameIsNotAnotherFile();

private:
    QString make(const QString &name, const QString &title);
    QStringList music() const;
    int inTrash() const;
    OrganiseOptions options() const;
    QTemporaryDir m_dir;
    QString m_work;
    int m_counter = 0;
};

void OrganiseOwnFilesMoreTest::init()
{
    QVERIFY(m_dir.isValid());
    m_work = m_dir.filePath(QStringLiteral("t%1").arg(++m_counter));
    QVERIFY(QDir().mkpath(m_work + QStringLiteral("/m")));
    qputenv("XDG_DATA_HOME", QFile::encodeName(m_work + QStringLiteral("/xdg-data")));
}

// m/<name>.flac, a copy of basic.flac whose title is `title`.
QString OrganiseOwnFilesMoreTest::make(const QString &name, const QString &title)
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
QStringList OrganiseOwnFilesMoreTest::music() const
{
    return QDir(m_work + QStringLiteral("/m"))
        .entryList(
            QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
}

int OrganiseOwnFilesMoreTest::inTrash() const
{
    return int(QDir(m_work + QStringLiteral("/xdg-data/Trash/files"))
            .entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot)
            .size());
}

OrganiseOptions OrganiseOwnFilesMoreTest::options() const
{
    OrganiseOptions o;
    o.namingTemplate = linuxbee::Template::parse(QStringLiteral("<Title>"));
    o.replaceExisting = true;
    return o;
}

void OrganiseOwnFilesMoreTest::aChainOfThree()
{
    // a is to be called b, b is to be called c, c is to be called d. b and c are files of the
    // run: a and b are numbered, and nothing is replaced.
    const QString a = make(s(u"a"), s(u"b"));
    const QString b = make(s(u"b"), s(u"c"));
    const QString c = make(s(u"c"), s(u"d"));
    const QByteArray wasA = bytesOf(a), wasB = bytesOf(b), wasC = bytesOf(c);
    const OrganiseOptions o = options();
    const QList<PlannedMove> plan = linuxbee::planOrganise({ a, b, c }, o);
    QCOMPARE(int(plan.size()), 3);
    QVERIFY(!at(plan, 0).replaces && !at(plan, 1).replaces && !at(plan, 2).replaces);
    const OrganiseReport r = linuxbee::executeOrganise(plan, o);
    QCOMPARE(r.done, 3);
    QCOMPARE(r.failed, 0);
    QCOMPARE(music(), QStringList({ s(u"b_1.flac"), s(u"c_1.flac"), s(u"d.flac") }));
    QCOMPARE(bytesOf(m_work + s(u"/m/b_1.flac")), wasA);
    QCOMPARE(bytesOf(m_work + s(u"/m/c_1.flac")), wasB);
    QCOMPARE(bytesOf(m_work + s(u"/m/d.flac")), wasC);
    QCOMPARE(inTrash(), 0);
}

void OrganiseOwnFilesMoreTest::aRingOfThree()
{
    // a is to be called b, b is to be called c, c is to be called a. The last takes the name
    // the first has left; the others are numbered.
    const QString a = make(s(u"a"), s(u"b"));
    const QString b = make(s(u"b"), s(u"c"));
    const QString c = make(s(u"c"), s(u"a"));
    const QByteArray wasA = bytesOf(a), wasB = bytesOf(b), wasC = bytesOf(c);
    const OrganiseOptions o = options();
    const QList<PlannedMove> plan = linuxbee::planOrganise({ a, b, c }, o);
    QVERIFY(!at(plan, 0).replaces && !at(plan, 1).replaces && !at(plan, 2).replaces);
    const OrganiseReport r = linuxbee::executeOrganise(plan, o);
    QCOMPARE(r.done, 3);
    QCOMPARE(r.failed, 0);
    QCOMPARE(music(), QStringList({ s(u"a.flac"), s(u"b_1.flac"), s(u"c_1.flac") }));
    QCOMPARE(bytesOf(m_work + s(u"/m/b_1.flac")), wasA);
    QCOMPARE(bytesOf(m_work + s(u"/m/c_1.flac")), wasB);
    QCOMPARE(bytesOf(m_work + s(u"/m/a.flac")), wasC);
    QCOMPARE(inTrash(), 0);
}

void OrganiseOwnFilesMoreTest::twoSwaps()
{
    // a and b change names, and c and d do, in one run.
    const QString a = make(s(u"a"), s(u"b"));
    const QString b = make(s(u"b"), s(u"a"));
    const QString c = make(s(u"c"), s(u"d"));
    const QString d = make(s(u"d"), s(u"c"));
    const QByteArray wasA = bytesOf(a), wasB = bytesOf(b), wasC = bytesOf(c), wasD = bytesOf(d);
    const OrganiseOptions o = options();
    const QList<PlannedMove> plan = linuxbee::planOrganise({ a, b, c, d }, o);
    for (qsizetype i = 0; i < 4; ++i)
        QVERIFY(!at(plan, i).replaces);
    const OrganiseReport r = linuxbee::executeOrganise(plan, o);
    QCOMPARE(r.done, 4);
    QCOMPARE(r.failed, 0);
    QCOMPARE(music(), QStringList({ s(u"a.flac"), s(u"b_1.flac"), s(u"c.flac"), s(u"d_1.flac") }));
    QCOMPARE(bytesOf(m_work + s(u"/m/b_1.flac")), wasA);
    QCOMPARE(bytesOf(m_work + s(u"/m/a.flac")), wasB);
    QCOMPARE(bytesOf(m_work + s(u"/m/d_1.flac")), wasC);
    QCOMPARE(bytesOf(m_work + s(u"/m/c.flac")), wasD);
    QCOMPARE(inTrash(), 0);
}

void OrganiseOwnFilesMoreTest::itsOwnSourceByAnotherNameIsNotAnotherFile()
{
    // O-EXECUTE: "...and not the entry's own source". b is a second name of the file a (a hard
    // link), and a is to be called b. The file at the target is a file of the run, but it is
    // this entry's own: the entry is carried out as any replacing is, and is not refused.
    // z is a second entry, so that the run has other files too.
    const QString a = make(s(u"a"), s(u"b"));
    const QString z = make(s(u"z"), s(u"q"));
    const QString b = m_work + s(u"/m/b.flac");
    QCOMPARE(::link(QFile::encodeName(a).constData(), QFile::encodeName(b).constData()), 0);
    const QByteArray wasA = bytesOf(a), wasZ = bytesOf(z);
    const OrganiseOptions o = options();
    const QList<PlannedMove> plan = linuxbee::planOrganise({ a, z }, o);
    QVERIFY(at(plan, 0).status == Status::Ready);
    QCOMPARE(at(plan, 0).target, b);
    QVERIFY(at(plan, 0).replaces);
    const OrganiseReport r = linuxbee::executeOrganise(plan, o);
    QCOMPARE(at(r.outcomes, 0).message, QString());
    QCOMPARE(r.done, 2);
    QCOMPARE(r.failed, 0);
    QVERIFY(at(r.outcomes, 0).replaced);
    QCOMPARE(music(), QStringList({ s(u"b.flac"), s(u"q.flac") }));
    QCOMPARE(bytesOf(b), wasA);
    QCOMPARE(bytesOf(m_work + s(u"/m/q.flac")), wasZ);
    QCOMPARE(inTrash(), 1); // the other name, with the same bytes: nothing is lost
}

QTEST_MAIN(OrganiseOwnFilesMoreTest)
#include "tst_organise_ownfiles_more.moc"
