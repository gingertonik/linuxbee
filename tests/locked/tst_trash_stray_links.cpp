// LOCKED ACCEPTANCE TEST (the trash: a second name is taken back only out of this trash's own folders). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/delete-files.md (FD-STAYED, "This
// trash's own folders"). The desktop library is stood in for by functions
// of this test. The trash is a folder of this test's own.
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

int linksTo(const QString &path)
{
    struct stat st;
    return ::lstat(QFile::encodeName(path).constData(), &st) == 0 ? int(st.st_nlink) : -1;
}

bool hardLink(const QString &from, const QString &to)
{
    return ::link(QFile::encodeName(from).constData(), QFile::encodeName(to).constData()) == 0;
}

}

class TrashStrayLinksTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void filesIsALink();
    void infoIsALink();
    void theTrashItselfMayBeLinked();
    void theSourceInsideTheTrash();
    void aLinkInTheTrashIsLeft();
    void onlyThePlaceNamed();

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
    QString made(const QString &relative)
    {
        QDir().mkpath(m_dir.filePath(relative));
        return m_dir.filePath(relative);
    }
    // What the desktop library did: a second name at `folder`/name, a record at `records`/name.trashinfo,
    // "moved", and it names the place as `namedFolder`/name.
    TrashMover secondName(const QString &folder, const QString &records, const QString &namedFolder)
    {
        return [=](const QString &real, QString *where) {
            const QString name = QFileInfo(real).fileName();
            if (!hardLink(real, folder + QLatin1Char('/') + name))
                return false;
            QFile record(records + QLatin1Char('/') + name + QStringLiteral(".trashinfo"));
            if (!record.open(QIODevice::WriteOnly))
                return false;
            record.write("[Trash Info]\n");
            *where = namedFolder + QLatin1Char('/') + name;
            return true;
        };
    }
    QTemporaryDir m_dir;
    QString m_data;
    int m_round = 0;
};

void TrashStrayLinksTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
}

void TrashStrayLinksTest::init()
{
    m_data = m_dir.filePath(QStringLiteral("xdg-data-%1").arg(++m_round));
    qputenv("XDG_DATA_HOME", QFile::encodeName(m_data));
    QDir().mkpath(m_data);
}

void TrashStrayLinksTest::filesIsALink()
{
    // FD-STAYED: a `files` that is a link leads out of the trash. Nothing is removed through it.
    const QString a = write(QStringLiteral("m1/a.flac"), "a");
    const QString elsewhere = made(QStringLiteral("elsewhere1"));
    QDir().mkpath(trash() + QStringLiteral("/info"));
    QVERIFY(QFile::link(elsewhere, trash() + QStringLiteral("/files")));
    QString where;
    const TrashMover mover = secondName(elsewhere, trash() + QStringLiteral("/info"), trash() + QStringLiteral("/files"));
    QVERIFY(sendToTrashWith(a, &where, mover) == SendResult::StillThere);
    QCOMPARE(linksTo(a), 2); // the name in the other folder was not taken away
    QCOMPARE(read(elsewhere + QStringLiteral("/a.flac")), QByteArray("a"));
    QVERIFY(QFileInfo::exists(trash() + QStringLiteral("/info/a.flac.trashinfo"))); // nor the record
    QCOMPARE(read(a), QByteArray("a"));
    // The same when the library names the place by the folder the link leads to.
    const QString b = write(QStringLiteral("m1/b.flac"), "b");
    QVERIFY(sendToTrashWith(b, &where, secondName(elsewhere, trash() + QStringLiteral("/info"), elsewhere)) == SendResult::StillThere);
    QCOMPARE(linksTo(b), 2);
}

void TrashStrayLinksTest::infoIsALink()
{
    // FD-STAYED: an `info` that is a link: nothing is removed, the second name neither.
    const QString a = write(QStringLiteral("m2/a.flac"), "a");
    const QString elsewhere = made(QStringLiteral("elsewhere2"));
    QDir().mkpath(trash() + QStringLiteral("/files"));
    QVERIFY(QFile::link(elsewhere, trash() + QStringLiteral("/info")));
    write(QStringLiteral("elsewhere2/keep.trashinfo"), "somebody's");
    QString where;
    QVERIFY(sendToTrashWith(a, &where, secondName(trash() + QStringLiteral("/files"), elsewhere, trash() + QStringLiteral("/files"))) == SendResult::StillThere);
    QCOMPARE(linksTo(a), 2);
    QVERIFY(QFileInfo::exists(elsewhere + QStringLiteral("/a.flac.trashinfo")));
    QCOMPARE(read(elsewhere + QStringLiteral("/keep.trashinfo")), QByteArray("somebody's"));
}

void TrashStrayLinksTest::theTrashItselfMayBeLinked()
{
    // FD-STAYED: the trash folder, or the data folder above it, may be a link (a home on another
    // disk). Its own `files` and `info` are real folders, and the second name is taken back.
    const QString a = write(QStringLiteral("m3/a.flac"), "a");
    const QString realTrash = made(QStringLiteral("real-trash-3"));
    QDir().mkpath(realTrash + QStringLiteral("/files"));
    QDir().mkpath(realTrash + QStringLiteral("/info"));
    QVERIFY(QFile::link(realTrash, trash()));
    QString where;
    QVERIFY(sendToTrashWith(a, &where, secondName(trash() + QStringLiteral("/files"), trash() + QStringLiteral("/info"), trash() + QStringLiteral("/files"))) == SendResult::StillThere);
    QCOMPARE(linksTo(a), 1);
    QVERIFY(!QFileInfo::exists(realTrash + QStringLiteral("/files/a.flac")));
    QVERIFY(!QFileInfo::exists(realTrash + QStringLiteral("/info/a.flac.trashinfo")));
    // And when the library names the place by the real folder.
    const QString b = write(QStringLiteral("m3/b.flac"), "b");
    QVERIFY(sendToTrashWith(b, &where, secondName(realTrash + QStringLiteral("/files"), realTrash + QStringLiteral("/info"), realTrash + QStringLiteral("/files"))) == SendResult::StillThere);
    QCOMPARE(linksTo(b), 1);
    QCOMPARE(read(b), QByteArray("b"));
}

void TrashStrayLinksTest::theSourceInsideTheTrash()
{
    // FD-STAYED: a file that is itself in the trash's files folder is its own "second name".
    // It is the only name the file has: it is never removed.
    QDir().mkpath(trash() + QStringLiteral("/info"));
    const QString inside = trash() + QStringLiteral("/files/x.flac");
    QDir().mkpath(trash() + QStringLiteral("/files"));
    {
        QFile f(inside);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("the only copy");
    }
    QString where;
    const QStringList spellings { inside, trash() + QStringLiteral("/files/../files/x.flac"), trash() + QStringLiteral("/files/./x.flac") };
    for (const QString &named : spellings) {
        const TrashMover same = [named](const QString &, QString *place) {
            *place = named;
            return true;
        };
        QVERIFY2(sendToTrashWith(inside, &where, same) == SendResult::StillThere, qPrintable(named));
        QCOMPARE(read(inside), QByteArray("the only copy"));
        QCOMPARE(linksTo(inside), 1);
    }
    // Reached through a linked folder, it is still the same name.
    QVERIFY(QFile::link(trash() + QStringLiteral("/files"), m_dir.filePath(QStringLiteral("into-trash-4"))));
    const QString via = m_dir.filePath(QStringLiteral("into-trash-4/x.flac"));
    const TrashMover same = [inside](const QString &, QString *place) {
        *place = inside;
        return true;
    };
    QVERIFY(sendToTrashWith(via, &where, same) == SendResult::StillThere);
    QCOMPARE(read(inside), QByteArray("the only copy"));
}

void TrashStrayLinksTest::aLinkInTheTrashIsLeft()
{
    // FD-STAYED: what the library named is a link in the trash that points at the file: not the same file, left.
    const QString a = write(QStringLiteral("m5/a.flac"), "a");
    QDir().mkpath(trash() + QStringLiteral("/files"));
    QDir().mkpath(trash() + QStringLiteral("/info"));
    const QString link = trash() + QStringLiteral("/files/a.flac");
    QVERIFY(QFile::link(a, link));
    const TrashMover names = [link](const QString &, QString *place) {
        *place = link;
        return true;
    };
    QString where;
    QVERIFY(sendToTrashWith(a, &where, names) == SendResult::StillThere);
    QVERIFY(QFileInfo(link).isSymLink());
    QCOMPARE(read(a), QByteArray("a"));
}

void TrashStrayLinksTest::onlyThePlaceNamed()
{
    // FD-STAYED: the library named a place that is not in this trash's files folder. A name in the
    // trash that happens to be the same file (from an earlier time) is not what it named: left.
    const QString a = write(QStringLiteral("m6/a.flac"), "a");
    const QString elsewhere = made(QStringLiteral("elsewhere6"));
    QDir().mkpath(trash() + QStringLiteral("/files"));
    QDir().mkpath(trash() + QStringLiteral("/info"));
    QVERIFY(hardLink(a, trash() + QStringLiteral("/files/a.flac")));
    QVERIFY(hardLink(a, elsewhere + QStringLiteral("/a.flac")));
    const TrashMover names = [elsewhere](const QString &, QString *place) {
        *place = elsewhere + QStringLiteral("/a.flac");
        return true;
    };
    QString where;
    QVERIFY(sendToTrashWith(a, &where, names) == SendResult::StillThere);
    QCOMPARE(linksTo(a), 3);
}

QTEST_GUILESS_MAIN(TrashStrayLinksTest)
#include "tst_trash_stray_links.moc"
