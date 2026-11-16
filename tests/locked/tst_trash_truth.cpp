// LOCKED ACCEPTANCE TEST (the trash: "it went" is said only when it went). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/delete-files.md (FD-SEND). The
// trash is a folder of this test's own: XDG_DATA_HOME points into a
// temporary folder for the whole run. The cases need no special rights;
// the read-only ones are skipped for a user the system lets write anywhere.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <sys/stat.h>
#include <unistd.h>

#include "files/trash.h"

using namespace linuxbee;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

using Status = TrashOutcome::Status;

// A folder nothing can be taken out of, until this goes out of scope.
struct ReadOnly {
    QString folder;
    explicit ReadOnly(const QString &path)
        : folder(path)
    {
        ::chmod(QFile::encodeName(folder).constData(), 0555);
    }
    ~ReadOnly() { ::chmod(QFile::encodeName(folder).constData(), 0755); }
};

int linksTo(const QString &path)
{
    struct stat st;
    return ::lstat(QFile::encodeName(path).constData(), &st) == 0 ? int(st.st_nlink) : -1;
}

}

class TrashTruthTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void aFolderThatCannotBeWritten();
    void throughALinkedFolder();
    void theHelperItself();

private:
    QString write(const QString &relative, const QByteArray &bytes)
    {
        const QString path = m_dir.filePath(relative);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size())
            QTest::qFail("could not write a file", __FILE__, __LINE__);
        return path;
    }
    static QByteArray read(const QString &path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<cannot be read>");
    }
    QStringList inTrash() const
    {
        return QDir(m_data + QStringLiteral("/Trash/files")).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System | QDir::Hidden);
    }
    QByteArray record(const QString &name) const { return read(m_data + QStringLiteral("/Trash/info/") + name + QStringLiteral(".trashinfo")); }
    QTemporaryDir m_dir;
    QString m_data;
    int m_round = 0;
};

void TrashTruthTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
}

void TrashTruthTest::init()
{
    m_data = m_dir.filePath(QStringLiteral("xdg-data-%1").arg(++m_round));
    qputenv("XDG_DATA_HOME", QFile::encodeName(m_data));
}

void TrashTruthTest::aFolderThatCannotBeWritten()
{
    // FD-SEND: a file cannot leave a folder that cannot be written. It is not tried, so nothing is left in the trash.
    if (::geteuid() == 0)
        QSKIP("this user may write anywhere");
    const QString a = write(QStringLiteral("locked/a.flac"), "stays a");
    const QString b = write(QStringLiteral("locked/b.flac"), "stays b");
    const QString free = write(QStringLiteral("free/c.flac"), "goes");
    QList<TrashOutcome> out;
    {
        const ReadOnly guard(m_dir.filePath(QStringLiteral("locked")));
        out = moveFilesToTrash({ a, b, free });
        QCOMPARE(linksTo(a), 1); // no second name for it anywhere
        QCOMPARE(linksTo(b), 1);
    }
    QCOMPARE(out.size(), 3);
    for (int i = 0; i < 2; ++i) {
        QVERIFY2(out.at(i).status == Status::Failed, qPrintable(out.at(i).path));
        QCOMPARE(out.at(i).reason, s(u"its folder cannot be written"));
        QCOMPARE(out.at(i).pathInTrash, QString());
    }
    QVERIFY(out.at(2).status == Status::Trashed); // the one after them still goes
    QCOMPARE(read(a), QByteArray("stays a"));
    QCOMPARE(read(b), QByteArray("stays b"));
    QCOMPARE(inTrash(), QStringList({ s(u"c.flac") })); // nothing of the two
}

void TrashTruthTest::throughALinkedFolder()
{
    // FD-SEND: a file reached through a linked folder goes, and its record names where it really was.
    const QString real = write(QStringLiteral("real/artist/album/x.flac"), "x");
    const QString realY = write(QStringLiteral("real/artist/album/y.flac"), "y");
    const QString link = m_dir.filePath(QStringLiteral("linked-album"));
    QVERIFY(QFile::link(m_dir.filePath(QStringLiteral("real/artist/album")), link));
    const QString top = m_dir.filePath(QStringLiteral("linked-top"));
    QVERIFY(QFile::link(m_dir.filePath(QStringLiteral("real")), top));
    const QString viaLink = link + QStringLiteral("/x.flac");
    const QString viaTop = top + QStringLiteral("/artist/album/y.flac");

    const QList<TrashOutcome> out = moveFilesToTrash({ viaLink, viaTop });
    QCOMPARE(out.size(), 2);
    QVERIFY(out.at(0).status == Status::Trashed);
    QVERIFY(out.at(1).status == Status::Trashed);
    QCOMPARE(out.at(0).path, viaLink); // the caller's path, so the caller knows which it was
    QCOMPARE(out.at(1).path, viaTop);
    QVERIFY(!QFileInfo::exists(real));
    QVERIFY(!QFileInfo::exists(realY));
    QVERIFY(QFileInfo(link).isSymLink()); // the links themselves are left
    QVERIFY(QFileInfo(top).isSymLink());
    QCOMPARE(read(out.at(0).pathInTrash), QByteArray("x"));
    const QString realFolder = QFileInfo(m_dir.filePath(QStringLiteral("real/artist/album"))).canonicalFilePath();
    QVERIFY(!realFolder.isEmpty());
    const QByteArray x = record(s(u"x.flac"));
    const QByteArray y = record(s(u"y.flac"));
    QVERIFY2(x.contains("Path=" + QFile::encodeName(realFolder + s(u"/x.flac")) + "\n"), x.constData());
    QVERIFY2(y.contains("Path=" + QFile::encodeName(realFolder + s(u"/y.flac")) + "\n"), y.constData());
    QVERIFY2(!x.contains("linked-album"), x.constData());
    QVERIFY2(!y.contains("linked-top"), y.constData());
}

void TrashTruthTest::theHelperItself()
{
    // FD-SEND: sendToTrash is what the organiser calls, so the same holds there.
    const QString real = write(QStringLiteral("h/real/z.ogg"), "z");
    const QString link = m_dir.filePath(QStringLiteral("h/link"));
    QVERIFY(QFile::link(m_dir.filePath(QStringLiteral("h/real")), link));
    QString where;
    QVERIFY(sendToTrash(link + QStringLiteral("/z.ogg"), &where));
    QVERIFY(!QFileInfo::exists(real));
    QCOMPARE(read(where), QByteArray("z"));
    QVERIFY(record(s(u"z.ogg")).contains(QFile::encodeName(QFileInfo(m_dir.filePath(QStringLiteral("h/real"))).canonicalFilePath() + s(u"/z.ogg"))));

    if (::geteuid() == 0)
        QSKIP("this user may write anywhere");
    const QString kept = write(QStringLiteral("h/locked/k.ogg"), "kept");
    where = QStringLiteral("untouched");
    {
        const ReadOnly guard(m_dir.filePath(QStringLiteral("h/locked")));
        QVERIFY(!sendToTrash(kept, &where)); // true would be a lie: the file is still there
        QCOMPARE(linksTo(kept), 1);
    }
    QCOMPARE(read(kept), QByteArray("kept"));
    QCOMPARE(inTrash(), QStringList({ s(u"z.ogg") }));
}

QTEST_GUILESS_MAIN(TrashTruthTest)
#include "tst_trash_truth.moc"
