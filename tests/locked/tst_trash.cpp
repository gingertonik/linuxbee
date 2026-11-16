// LOCKED ACCEPTANCE TEST (moving files to the trash). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/delete-files.md (FD-TRASH). The
// trash is a folder of this test's own: XDG_DATA_HOME points into a
// temporary folder for the whole run, so the desktop's real trash is
// never touched.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <sys/stat.h>

#include "files/trash.h"

using namespace linuxbee;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

using Status = TrashOutcome::Status;

}

class TrashTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void aFileGoesToTheTrash();
    void twoOfOneName();
    void whatIsNotARegularFile();
    void notThere();
    void eachPathOnce();
    void theTrashCannotTakeIt();
    void noFolderIsRemoved();
    void theOrganisersHelper();

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
    QString trash() const { return m_data + QStringLiteral("/Trash"); }
    QTemporaryDir m_dir;
    QString m_data;
    int m_round = 0;
};

void TrashTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
}

void TrashTest::init()
{
    // A trash of its own for each test, not made yet.
    m_data = m_dir.filePath(QStringLiteral("xdg-data-%1").arg(++m_round));
    qputenv("XDG_DATA_HOME", QFile::encodeName(m_data));
}

void TrashTest::aFileGoesToTheTrash()
{
    // FD-TRASH: under its own name, with a record of where it came from; the trash folders are made when missing.
    const QString path = write(QStringLiteral("one/album/song.flac"), "the bytes of a song");
    QVERIFY(!QFileInfo::exists(trash()));
    const QList<TrashOutcome> out = moveFilesToTrash({ path });
    QCOMPARE(out.size(), 1);
    QCOMPARE(out.at(0).path, path);
    QVERIFY(out.at(0).status == Status::Trashed);
    QCOMPARE(out.at(0).reason, QString());
    QVERIFY(!QFileInfo::exists(path));
    const QFileInfo in(out.at(0).pathInTrash);
    QVERIFY2(in.isFile(), qPrintable(out.at(0).pathInTrash));
    QCOMPARE(in.absolutePath(), trash() + QStringLiteral("/files"));
    QCOMPARE(in.fileName(), s(u"song.flac"));
    QCOMPARE(read(in.absoluteFilePath()), QByteArray("the bytes of a song"));
    const QByteArray record = read(trash() + QStringLiteral("/info/song.flac.trashinfo"));
    QVERIFY2(record.contains("[Trash Info]"), record.constData());
    QVERIFY2(record.contains("Path=" + QFile::encodeName(path)), record.constData()); // so that a restore puts it back
    QVERIFY2(record.contains("DeletionDate="), record.constData());
    // A path that is not clean, or not absolute, is the same file.
    const QString other = write(QStringLiteral("one/album/other.flac"), "x");
    const QString unclean = m_dir.filePath(QStringLiteral("one/./album/../album/other.flac"));
    const QList<TrashOutcome> again = moveFilesToTrash({ unclean });
    QCOMPARE(again.at(0).path, other);
    QVERIFY(again.at(0).status == Status::Trashed);
    QVERIFY(!QFileInfo::exists(other));
    QCOMPARE(moveFilesToTrash({ }).size(), 0);
}

void TrashTest::twoOfOneName()
{
    // FD-TRASH: two files of one name are both kept in the trash.
    const QString a = write(QStringLiteral("a/track.mp3"), "from a");
    const QString b = write(QStringLiteral("b/track.mp3"), "from b");
    const QList<TrashOutcome> out = moveFilesToTrash({ a, b });
    QCOMPARE(out.size(), 2);
    QVERIFY(out.at(0).status == Status::Trashed && out.at(1).status == Status::Trashed);
    QVERIFY(out.at(0).pathInTrash != out.at(1).pathInTrash);
    QCOMPARE(read(out.at(0).pathInTrash), QByteArray("from a"));
    QCOMPARE(read(out.at(1).pathInTrash), QByteArray("from b"));
    QCOMPARE(QDir(trash() + QStringLiteral("/files")).entryList(QDir::Files).size(), 2);
    QCOMPARE(QDir(trash() + QStringLiteral("/info")).entryList(QDir::Files).size(), 2);
}

void TrashTest::whatIsNotARegularFile()
{
    // FD-TRASH: a folder, a link and a special file are refused, untouched, and do not hide the file after them.
    const QString inside = write(QStringLiteral("refuse/folder/kept.flac"), "kept");
    const QString folder = m_dir.filePath(QStringLiteral("refuse/folder"));
    const QString target = write(QStringLiteral("refuse/target.flac"), "the target");
    const QString link = m_dir.filePath(QStringLiteral("refuse/link.flac"));
    QVERIFY(QFile::link(target, link));
    const QString broken = m_dir.filePath(QStringLiteral("refuse/broken.flac"));
    QVERIFY(QFile::link(m_dir.filePath(QStringLiteral("refuse/nothing-here")), broken));
    const QString folderLink = m_dir.filePath(QStringLiteral("refuse/folder-link"));
    QVERIFY(QFile::link(folder, folderLink));
    const QString pipe = m_dir.filePath(QStringLiteral("refuse/pipe.flac"));
    QCOMPARE(::mkfifo(QFile::encodeName(pipe).constData(), 0600), 0);
    const QString last = write(QStringLiteral("refuse/last.flac"), "last");

    const QList<TrashOutcome> out = moveFilesToTrash({ folder, link, broken, folderLink, pipe, QStringLiteral("/dev/null"), last });
    QCOMPARE(out.size(), 7);
    const QStringList reasons { s(u"a folder"), s(u"a link"), s(u"a link"), s(u"a link"), s(u"not a regular file"), s(u"not a regular file") };
    for (int i = 0; i < 6; ++i) {
        QVERIFY2(out.at(i).status == Status::Refused, qPrintable(out.at(i).path));
        QCOMPARE(out.at(i).reason, reasons.at(i));
        QCOMPARE(out.at(i).pathInTrash, QString());
    }
    QVERIFY(out.at(6).status == Status::Trashed);
    // Everything refused is as it was.
    QCOMPARE(read(inside), QByteArray("kept"));
    QCOMPARE(read(target), QByteArray("the target"));
    QVERIFY(QFileInfo(link).isSymLink());
    QVERIFY(QFileInfo(broken).isSymLink());
    QVERIFY(QFileInfo(folderLink).isSymLink());
    struct stat st;
    QCOMPARE(::lstat(QFile::encodeName(pipe).constData(), &st), 0);
    QVERIFY(S_ISFIFO(st.st_mode));
    QVERIFY(QFileInfo::exists(QStringLiteral("/dev/null")));
    QCOMPARE(QDir(trash() + QStringLiteral("/files")).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System), QStringList({ s(u"last.flac") }));
}

void TrashTest::notThere()
{
    // FD-TRASH: nothing at the path is its own answer, and makes no trash entry.
    const QString gone = m_dir.filePath(QStringLiteral("gone/never.flac"));
    const QList<TrashOutcome> out = moveFilesToTrash({ gone, QString() });
    QCOMPARE(out.size(), 1); // an empty path is no path
    QVERIFY(out.at(0).status == Status::NotThere);
    QCOMPARE(out.at(0).path, gone);
    QCOMPARE(out.at(0).reason, QString());
    QVERIFY(QDir(trash() + QStringLiteral("/files")).entryList(QDir::Files).isEmpty());
}

void TrashTest::eachPathOnce()
{
    // FD-TRASH: a path given twice, however it is spelt, is one file and one outcome.
    const QString a = write(QStringLiteral("twice/a.flac"), "a");
    const QString b = write(QStringLiteral("twice/b.flac"), "b");
    const QString aAgain = m_dir.filePath(QStringLiteral("twice/../twice/a.flac"));
    const QList<TrashOutcome> out = moveFilesToTrash({ a, b, aAgain, a });
    QCOMPARE(out.size(), 2);
    QCOMPARE(out.at(0).path, a);
    QCOMPARE(out.at(1).path, b);
    QVERIFY(out.at(0).status == Status::Trashed); // not "not there" from a second try
    QVERIFY(out.at(1).status == Status::Trashed);
}

void TrashTest::theTrashCannotTakeIt()
{
    // FD-TRASH: when the trash fails the file stays, byte for byte. It is never removed instead.
    const QString a = write(QStringLiteral("stuck/a.flac"), "must stay");
    const QString b = write(QStringLiteral("stuck/b.flac"), "must stay too");
    QFile blocker(m_data); // a file where the data folder should be: no trash can be made
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    const QList<TrashOutcome> out = moveFilesToTrash({ a, b });
    QCOMPARE(out.size(), 2);
    for (const TrashOutcome &o : out) {
        QVERIFY(o.status == Status::Failed);
        QCOMPARE(o.reason, s(u"the trash did not take it"));
        QCOMPARE(o.pathInTrash, QString());
    }
    QCOMPARE(read(a), QByteArray("must stay"));
    QCOMPARE(read(b), QByteArray("must stay too"));
}

void TrashTest::noFolderIsRemoved()
{
    // FD-TRASH: a folder that is empty afterwards stays.
    const QString only = write(QStringLiteral("empties/artist/album/only.flac"), "only");
    QVERIFY(moveFilesToTrash({ only }).at(0).status == Status::Trashed);
    QVERIFY(QFileInfo(m_dir.filePath(QStringLiteral("empties/artist/album"))).isDir());
    QVERIFY(QDir(m_dir.filePath(QStringLiteral("empties/artist/album"))).isEmpty());
}

void TrashTest::theOrganisersHelper()
{
    // sendToTrash: the one place a file is handed to the trash.
    const QString path = write(QStringLiteral("helper/x.ogg"), "x");
    QString where;
    QVERIFY(sendToTrash(path, &where));
    QVERIFY(!QFileInfo::exists(path));
    QCOMPARE(QFileInfo(where).absolutePath(), trash() + QStringLiteral("/files"));
    QCOMPARE(read(where), QByteArray("x"));
    QVERIFY(!sendToTrash(m_dir.filePath(QStringLiteral("helper/none.ogg")), &where));
}

QTEST_GUILESS_MAIN(TrashTest)
#include "tst_trash.moc"
