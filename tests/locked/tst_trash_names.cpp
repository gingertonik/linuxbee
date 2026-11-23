// LOCKED ACCEPTANCE TEST (the trash and names it cannot take, fix-c1). Written by the reviewer, not
// the builder. The builder makes these pass by changing src/, never by editing this file. A test
// believed to be wrong is reported, with the reason, and left alone.
//
// Expected values: docs/spec/delete-files.md (FD-TRASH, FD-NAMES, NM-UTF8). Trash F4 and F5 of the
// audit: a name that is not UTF-8 was "not there", and a name of more than 245 bytes "the trash did
// not take it". The trash is a folder of this test's own (XDG_DATA_HOME, in the same temporary
// folder as the files, so on the same disk), and the files are made here.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "files/trash.h"

using linuxbee::TrashOutcome;

namespace {

QString nameOf(int bytes, char letter, const char *ext)
{
    QString name(bytes - int(qstrlen(ext)), QLatin1Char(letter));
    return name + QLatin1String(ext);
}

bool put(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

// Makes a file or folder whose name is given as bytes, and answers its path as linuxBee reads it.
bool makeRaw(const QByteArray &path, bool folder)
{
    if (folder)
        return ::mkdir(path.constData(), 0755) == 0;
    const int fd = ::open(path.constData(), O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0)
        return false;
    const bool wrote = ::write(fd, "music", 5) == 5;
    ::close(fd);
    return wrote;
}

bool thereRaw(const QByteArray &path)
{
    struct stat st;
    return ::lstat(path.constData(), &st) == 0;
}

}

class TrashNamesTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void aNameOf245BytesGoes();
    void aLongNameSaysWhy_data();
    void aLongNameSaysWhy();
    void aNameTakenInTheTrash();
    void aNameThatIsNotUtf8();
    void aFolderNameThatIsNotUtf8();
    void whatIsNotThereIsNotThere();

private:
    QStringList inTrash(const char *part) const;
    QTemporaryDir *m_dir = nullptr;
    QString m_music;
};

void TrashNamesTest::init()
{
    m_dir = new QTemporaryDir;
    QVERIFY(m_dir->isValid());
    for (const char *name : { "XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME" })
        qputenv(name, QFile::encodeName(m_dir->path() + QLatin1Char('/') + QLatin1String(name)));
    m_music = m_dir->path() + QStringLiteral("/music");
    QVERIFY(QDir().mkpath(m_music));
}

void TrashNamesTest::cleanup()
{
    delete m_dir;
    m_dir = nullptr;
}

QStringList TrashNamesTest::inTrash(const char *part) const
{
    return QDir(m_dir->path() + QStringLiteral("/XDG_DATA_HOME/Trash/") + QLatin1String(part))
        .entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
}

void TrashNamesTest::aNameOf245BytesGoes()
{
    // FD-NAMES, the other side: 245 bytes and its record's 10 make 255. It goes.
    const QString path = m_music + QLatin1Char('/') + nameOf(245, 'a', ".mp3");
    QVERIFY(put(path, "music"));
    const QList<TrashOutcome> out = linuxbee::moveFilesToTrash({ path });
    QCOMPARE(out.size(), 1);
    QVERIFY2(out.at(0).status == TrashOutcome::Status::Trashed, qPrintable(out.at(0).reason));
    QVERIFY(!QFileInfo::exists(path));
}

void TrashNamesTest::aLongNameSaysWhy_data()
{
    QTest::addColumn<int>("bytes");
    QTest::newRow("246 bytes") << 246;
    QTest::newRow("255 bytes") << 255;
}

void TrashNamesTest::aLongNameSaysWhy()
{
    // FD-NAMES: never handed to the trash; the file stays byte for byte, and the reason is said.
    QFETCH(int, bytes);
    const QString path = m_music + QLatin1Char('/') + nameOf(bytes, 'b', ".mp3");
    QVERIFY(put(path, "music"));
    const QList<TrashOutcome> out = linuxbee::moveFilesToTrash({ path });
    QCOMPARE(out.size(), 1);
    QCOMPARE(out.at(0).status, TrashOutcome::Status::Failed);
    QCOMPARE(out.at(0).reason,
        QStringLiteral(
            "the name is too long for the trash: %1 bytes, and the trash takes names of up to 245")
            .arg(bytes));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("music"));
    QCOMPARE(inTrash("files"), QStringList());
    QCOMPARE(inTrash("info"), QStringList());
}

void TrashNamesTest::aNameTakenInTheTrash()
{
    // FD-NAMES: two files of one 240-byte name, from two folders. The first goes. The second goes
    // under a name of its own, or stays and says why; never "the trash did not take it".
    const QString name = nameOf(240, 'c', ".mp3");
    QVERIFY(QDir().mkpath(m_music + QStringLiteral("/x")));
    QVERIFY(QDir().mkpath(m_music + QStringLiteral("/y")));
    const QString first = m_music + QStringLiteral("/x/") + name;
    const QString second = m_music + QStringLiteral("/y/") + name;
    QVERIFY(put(first, "one"));
    QVERIFY(put(second, "two"));
    QCOMPARE(linuxbee::moveFilesToTrash({ first }).value(0).status, TrashOutcome::Status::Trashed);
    const TrashOutcome out = linuxbee::moveFilesToTrash({ second }).value(0);
    if (out.status == TrashOutcome::Status::Trashed) {
        QVERIFY(!QFileInfo::exists(second));
        QVERIFY(QFileInfo(out.pathInTrash).fileName() != name);
        return;
    }
    QCOMPARE(out.status, TrashOutcome::Status::Failed);
    QCOMPARE(out.reason,
        QStringLiteral("a file of this name is in the trash already, and the trash could not give "
                       "this one a name of its own"));
    QFile file(second);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("two"));
}

void TrashNamesTest::aNameThatIsNotUtf8()
{
    // NM-UTF8: the file is there; linuxBee reads its name with a stand-in letter, and so cannot
    // reach it. It is refused with the reason, and stays.
    const QByteArray raw = QFile::encodeName(m_music) + "/bad\xff\xfe.mp3";
    QVERIFY(makeRaw(raw, false));
    const QString path = QString::fromUtf8(raw); // as a command line or a dialog gives it
    const QList<TrashOutcome> out = linuxbee::moveFilesToTrash({ path });
    QCOMPARE(out.size(), 1);
    QCOMPARE(out.at(0).status, TrashOutcome::Status::Refused);
    QCOMPARE(out.at(0).reason, QStringLiteral("its name is not valid UTF-8"));
    QVERIFY(thereRaw(raw));
    QCOMPARE(inTrash("files"), QStringList());
}

void TrashNamesTest::aFolderNameThatIsNotUtf8()
{
    // NM-UTF8: the same for a folder in the path.
    const QByteArray folder = QFile::encodeName(m_music) + "/d\xff";
    QVERIFY(makeRaw(folder, true));
    QVERIFY(makeRaw(folder + "/a.mp3", false));
    const QList<TrashOutcome> out
        = linuxbee::moveFilesToTrash({ QString::fromUtf8(folder + "/a.mp3") });
    QCOMPARE(out.value(0).status, TrashOutcome::Status::Refused);
    QCOMPARE(
        out.value(0).reason, QStringLiteral("the name of a folder in its path is not valid UTF-8"));
    QVERIFY(thereRaw(folder + "/a.mp3"));
}

void TrashNamesTest::whatIsNotThereIsNotThere()
{
    // The other side: nothing at the path, and no name in its folder that reads as it.
    // - a plain name;
    // - a name with the stand-in letter (U+FFFD, which is valid UTF-8), in a folder whose one name
    //   that is not UTF-8 reads as another;
    // - such a name, when the name that is not UTF-8 is in another folder.
    QVERIFY(QDir().mkpath(m_music + QStringLiteral("/other")));
    QVERIFY(makeRaw(QFile::encodeName(m_music) + "/other/bad\xff\xfe.mp3", false));
    QVERIFY(
        makeRaw(QFile::encodeName(m_music) + "/zz\xff.mp3", false)); // not UTF-8, but another name
    QVERIFY(put(m_music + QStringLiteral("/ok.mp3"), "music"));
    const QString plain = m_music + QStringLiteral("/gone.mp3");
    const QString standIn = m_music + QStringLiteral("/bad\uFFFD\uFFFD.mp3");
    const QList<TrashOutcome> out = linuxbee::moveFilesToTrash({ plain, standIn });
    QCOMPARE(out.size(), 2);
    QCOMPARE(out.at(0).status, TrashOutcome::Status::NotThere);
    QCOMPARE(out.at(1).status, TrashOutcome::Status::NotThere);
    QCOMPARE(out.at(1).reason, QString());
}

QTEST_GUILESS_MAIN(TrashNamesTest)
#include "tst_trash_names.moc"
