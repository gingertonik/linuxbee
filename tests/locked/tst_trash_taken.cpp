// LOCKED ACCEPTANCE TEST (the trash: a file that is in the trash with no record is not written
// over). Written by the reviewer, not the builder. The builder makes these pass by changing src/,
// never by editing this file. A test believed to be wrong is reported, with the reason, and left
// alone.
//
// Expected values are FD-TAKEN in docs/spec/delete-files.md. The fault this holds shut (the
// write-path audit, part 6, F2): the desktop library looks only at the records to know which names
// are taken. A file in the trash's `files` folder with no record (left by another tool, or its
// record removed by hand) was replaced by a new file of the same name, and was gone.
//
// The trash is a folder of this test's own: XDG_DATA_HOME points into a temporary folder, which is
// on the same disk as the files sent, so that it is the trash the library uses. The real library
// does the moving.
#include <QtTest>

#include <unistd.h>

#include "files/trash.h"

using linuxbee::SendResult;

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

bool put(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

}

class TrashTakenTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void aFileWithNoRecord();
    void aLinkWithNoRecord();
    void aFolderWithNoRecord();
    void severalInARow();
    void aNameWithARecordAsBefore();
    void aNameThatIsFreeAsBefore();
    void whenTheLibrarySaysNo();
    void whenTheNameCannotBeHeld();

private:
    QStringList in(const char *sub) const;
    QTemporaryDir m_dir;
    QString m_work, m_files, m_info;
    int m_counter = 0;
};

void TrashTakenTest::init()
{
    QVERIFY(m_dir.isValid());
    m_work = m_dir.filePath(QStringLiteral("t%1").arg(++m_counter));
    QVERIFY(QDir().mkpath(m_work + s(u"/m")));
    qputenv("XDG_DATA_HOME", QFile::encodeName(m_work + s(u"/data")));
    m_files = m_work + s(u"/data/Trash/files");
    m_info = m_work + s(u"/data/Trash/info");
    QVERIFY(QDir().mkpath(m_files) && QDir().mkpath(m_info));
}

QStringList TrashTakenTest::in(const char *sub) const
{
    return QDir(m_work + s(u"/data/Trash/") + QLatin1String(sub))
        .entryList(
            QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
}

void TrashTakenTest::aFileWithNoRecord()
{
    // FD-TAKEN: the audit's case. The file that was there is there, byte for byte; the file sent is
    // in the trash under another name, with its record; and no record was left for the file that
    // had none.
    QVERIFY(put(m_files + s(u"/a.mp3"), "somebody else's file"));
    const QString source = m_work + s(u"/m/a.mp3");
    QVERIFY(put(source, "the file sent"));
    QString where;
    QVERIFY(linuxbee::sendToTrash(source, &where));
    QVERIFY(!QFileInfo::exists(source));
    QCOMPARE(bytesOf(m_files + s(u"/a.mp3")), QByteArray("somebody else's file"));
    QVERIFY2(where != m_files + s(u"/a.mp3"), "the place named is the file that was there");
    QCOMPARE(QFileInfo(where).absolutePath(), m_files);
    QCOMPARE(bytesOf(where), QByteArray("the file sent"));
    QCOMPARE(int(in("files").size()), 2);
    // One record, and it is the sent file's: it names where the file came from.
    QCOMPARE(in("info"), QStringList({ QFileInfo(where).fileName() + s(u".trashinfo") }));
    const QByteArray record = bytesOf(m_info + QLatin1Char('/') + in("info").value(0));
    QVERIFY2(record.contains("Path=" + QFile::encodeName(source)), record.constData());
}

void TrashTakenTest::aLinkWithNoRecord()
{
    // A link in the trash with no record is something too: it stays a link, and what it points to
    // is not written.
    const QString elsewhere = m_work + s(u"/elsewhere.mp3");
    QVERIFY(put(elsewhere, "pointed to"));
    QVERIFY(QFile::link(elsewhere, m_files + s(u"/a.mp3")));
    const QString source = m_work + s(u"/m/a.mp3");
    QVERIFY(put(source, "the file sent"));
    QString where;
    QVERIFY(linuxbee::sendToTrash(source, &where));
    QVERIFY(QFileInfo(m_files + s(u"/a.mp3")).isSymLink());
    QCOMPARE(bytesOf(elsewhere), QByteArray("pointed to"));
    QCOMPARE(bytesOf(where), QByteArray("the file sent"));
    QVERIFY(where != m_files + s(u"/a.mp3"));
}

void TrashTakenTest::aFolderWithNoRecord()
{
    QVERIFY(put(m_files + s(u"/a.mp3/inside.txt"), "kept"));
    const QString source = m_work + s(u"/m/a.mp3");
    QVERIFY(put(source, "the file sent"));
    QString where;
    QVERIFY(linuxbee::sendToTrash(source, &where));
    QCOMPARE(bytesOf(m_files + s(u"/a.mp3/inside.txt")), QByteArray("kept"));
    QCOMPARE(bytesOf(where), QByteArray("the file sent"));
}

void TrashTakenTest::severalInARow()
{
    // Three files of one name, one after another, onto a name with no record: four files in the
    // end.
    QVERIFY(put(m_files + s(u"/a.mp3"), "somebody else's file"));
    QStringList places;
    for (int i = 1; i <= 3; ++i) {
        const QString source = m_work + s(u"/m/a.mp3");
        QVERIFY(put(source, "sent " + QByteArray::number(i)));
        QString where;
        QVERIFY(linuxbee::sendToTrash(source, &where));
        QCOMPARE(bytesOf(where), "sent " + QByteArray::number(i));
        places.append(where);
    }
    QCOMPARE(bytesOf(m_files + s(u"/a.mp3")), QByteArray("somebody else's file"));
    for (int i = 0; i < 3; ++i)
        QCOMPARE(bytesOf(places.at(i)), "sent " + QByteArray::number(i + 1));
    QCOMPARE(int(in("files").size()), 4);
    QCOMPARE(int(in("info").size()), 3);
}

void TrashTakenTest::aNameWithARecordAsBefore()
{
    // The other side: a name that is taken with its record is the library's own business, as it
    // was.
    const QString first = m_work + s(u"/m/a.mp3");
    QVERIFY(put(first, "first"));
    QString one, two;
    QVERIFY(linuxbee::sendToTrash(first, &one));
    QCOMPARE(one, m_files + s(u"/a.mp3"));
    QVERIFY(put(first, "second"));
    QVERIFY(linuxbee::sendToTrash(first, &two));
    QVERIFY(one != two);
    QCOMPARE(bytesOf(one), QByteArray("first"));
    QCOMPARE(bytesOf(two), QByteArray("second"));
    QCOMPARE(int(in("info").size()), 2);
}

void TrashTakenTest::aNameThatIsFreeAsBefore()
{
    // A file keeps its name in the trash when the name is free, and nothing but its own record is
    // made.
    QVERIFY(put(m_files + s(u"/other.mp3"), "somebody else's file"));
    const QString source = m_work + s(u"/m/a.mp3");
    QVERIFY(put(source, "the file sent"));
    QString where;
    QVERIFY(linuxbee::sendToTrash(source, &where));
    QCOMPARE(where, m_files + s(u"/a.mp3"));
    QCOMPARE(in("info"), QStringList({ s(u"a.mp3.trashinfo") }));
    QCOMPARE(bytesOf(m_files + s(u"/other.mp3")), QByteArray("somebody else's file"));
}

void TrashTakenTest::whenTheLibrarySaysNo()
{
    // FD-TAKEN: whatever came of it, nothing is left in `info` for the file that had no record.
    QVERIFY(put(m_files + s(u"/a.mp3"), "somebody else's file"));
    const QString source = m_work + s(u"/m/a.mp3");
    QVERIFY(put(source, "the file sent"));
    QString where;
    const SendResult r = linuxbee::sendToTrashWith(
        source, &where, [](const QString &, QString *) { return false; });
    QVERIFY(r == SendResult::NotTaken);
    QCOMPARE(bytesOf(source), QByteArray("the file sent"));
    QCOMPARE(bytesOf(m_files + s(u"/a.mp3")), QByteArray("somebody else's file"));
    QCOMPARE(in("info"), QStringList());
    QCOMPARE(in("files"), QStringList({ s(u"a.mp3") }));
}

void TrashTakenTest::whenTheNameCannotBeHeld()
{
    // FD-TAKEN: when the trash cannot be made safe for the file (its `info` folder cannot be
    // written), the file is not handed over at all. It stays, and so does the file in the trash.
    QVERIFY2(::geteuid() != 0, "this test cannot be run as root");
    QVERIFY(put(m_files + s(u"/a.mp3"), "somebody else's file"));
    const QString source = m_work + s(u"/m/a.mp3");
    QVERIFY(put(source, "the file sent"));
    QVERIFY(QFile::setPermissions(m_info, QFile::ReadOwner | QFile::ExeOwner));
    bool asked = false;
    QString where;
    const SendResult r
        = linuxbee::sendToTrashWith(source, &where, [&asked](const QString &, QString *) {
              asked = true;
              return false;
          });
    QFile::setPermissions(m_info, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QVERIFY(r == SendResult::NotTaken);
    QVERIFY2(!asked, "the file was handed over though the name could not be held");
    QCOMPARE(bytesOf(source), QByteArray("the file sent"));
    QCOMPARE(bytesOf(m_files + s(u"/a.mp3")), QByteArray("somebody else's file"));
}

QTEST_GUILESS_MAIN(TrashTakenTest)
#include "tst_trash_taken.moc"
