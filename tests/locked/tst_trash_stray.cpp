// LOCKED ACCEPTANCE TEST (the trash: when the desktop library says "moved" and the file stayed). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/delete-files.md (FD-SEND, FD-STAYED).
// The desktop library is stood in for by functions of this test, so that
// what it does wrong can be done here on purpose. The trash is a folder of
// this test's own.
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

int linksTo(const QString &path)
{
    struct stat st;
    return ::lstat(QFile::encodeName(path).constData(), &st) == 0 ? int(st.st_nlink) : -1;
}

}

class TrashStrayTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void aSecondNameIsTakenBack();
    void aCopyIsLeft();
    void nothingOutsideTheTrashIsTouched();
    void whatIsNotAFileNameIsNotTried();
    void theRealOne();

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
    QString files() const { return m_data + QStringLiteral("/Trash/files"); }
    QString info() const { return m_data + QStringLiteral("/Trash/info"); }
    static QStringList listed(const QString &folder)
    {
        return QDir(folder).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System | QDir::Hidden);
    }
    // What the desktop library did in a folder it could not take the file out of:
    // a second name in the trash, a record, "moved", and the file where it was.
    TrashMover secondName()
    {
        return [this](const QString &real, QString *where) {
            const QString name = QFileInfo(real).fileName();
            if (::link(QFile::encodeName(real).constData(), QFile::encodeName(files() + QLatin1Char('/') + name).constData()) != 0)
                return false;
            QFile record(info() + QLatin1Char('/') + name + QStringLiteral(".trashinfo"));
            if (!record.open(QIODevice::WriteOnly))
                return false;
            record.write("[Trash Info]\nPath=" + QFile::encodeName(real) + "\n");
            *where = files() + QLatin1Char('/') + name;
            ++m_calls;
            m_handed = real;
            return true;
        };
    }
    QTemporaryDir m_dir;
    QString m_data;
    int m_round = 0;
    int m_calls = 0;
    QString m_handed;
};

void TrashStrayTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
}

void TrashStrayTest::init()
{
    m_data = m_dir.filePath(QStringLiteral("xdg-data-%1").arg(++m_round));
    qputenv("XDG_DATA_HOME", QFile::encodeName(m_data));
    m_calls = 0;
    m_handed.clear();
}

void TrashStrayTest::aSecondNameIsTakenBack()
{
    // FD-STAYED: "moved", the file stayed, and the trash holds a second name for the very same file.
    // The second name and its record go; the file is where it was, whole, with one name.
    const QString a = write(QStringLiteral("m/a.flac"), "the bytes of a");
    QVERIFY(QFile::link(m_dir.filePath(QStringLiteral("m")), m_dir.filePath(QStringLiteral("via"))));
    const QString given = m_dir.filePath(QStringLiteral("via/a.flac"));
    QString where = s(u"untouched");
    QVERIFY(sendToTrashWith(given, &where, secondName()) == SendResult::StillThere);
    QCOMPARE(m_calls, 1);
    QCOMPARE(m_handed, QFileInfo(a).canonicalFilePath()); // FD-SEND 2: by the real folder
    QCOMPARE(where, s(u"untouched")); // nothing is said to be in the trash
    QCOMPARE(read(a), QByteArray("the bytes of a"));
    QCOMPARE(linksTo(a), 1);
    QCOMPARE(listed(files()), QStringList());
    QCOMPARE(listed(info()), QStringList());

    // Through the list: failed, with a reason that is true.
    const QString b = write(QStringLiteral("m/b.flac"), "b");
    const QString c = write(QStringLiteral("m/c.flac"), "c");
    const QList<TrashOutcome> out = moveFilesToTrashWith({ b, c }, secondName());
    QCOMPARE(out.size(), 2);
    for (const TrashOutcome &o : out) {
        QVERIFY(o.status == Status::Failed);
        QCOMPARE(o.reason, s(u"the file is still in its place"));
        QCOMPARE(o.pathInTrash, QString());
    }
    QCOMPARE(linksTo(b), 1);
    QCOMPARE(linksTo(c), 1);
    QCOMPARE(listed(files()), QStringList());
    QCOMPARE(listed(info()), QStringList());
}

void TrashStrayTest::aCopyIsLeft()
{
    // FD-STAYED: what is in the trash is another file (a copy). It is not this program's to remove.
    const QString a = write(QStringLiteral("k/a.flac"), "original");
    const TrashMover copies = [this](const QString &real, QString *where) {
        *where = files() + QStringLiteral("/a.flac");
        return QFile::copy(real, *where);
    };
    QString where;
    QVERIFY(sendToTrashWith(a, &where, copies) == SendResult::StillThere);
    QCOMPARE(read(a), QByteArray("original"));
    QCOMPARE(read(files() + QStringLiteral("/a.flac")), QByteArray("original"));
    QCOMPARE(where, QString());
}

void TrashStrayTest::nothingOutsideTheTrashIsTouched()
{
    // FD-STAYED: only a name inside this trash's files folder is ever taken back,
    // whatever place the library names.
    const QString a = write(QStringLiteral("o/a.flac"), "a");
    const QString other = m_dir.filePath(QStringLiteral("o/second-name.flac"));
    QCOMPARE(::link(QFile::encodeName(a).constData(), QFile::encodeName(other).constData()), 0);
    const QString bystander = write(QStringLiteral("o/a.flac.trashinfo"), "not a record");
    const TrashMover lies = [other](const QString &, QString *where) {
        *where = other; // the same file, but not in the trash
        return true;
    };
    QString where;
    QVERIFY(sendToTrashWith(a, &where, lies) == SendResult::StillThere);
    QCOMPARE(linksTo(a), 2);
    QCOMPARE(read(other), QByteArray("a"));
    QCOMPARE(read(bystander), QByteArray("not a record"));
    // A place given with ".." that leaves the trash is outside it too.
    const TrashMover dots = [this, other](const QString &, QString *where) {
        *where = files() + QStringLiteral("/../../../o/second-name.flac");
        return true;
    };
    QVERIFY(sendToTrashWith(a, &where, dots) == SendResult::StillThere);
    QCOMPARE(linksTo(a), 2);
    // No place named at all.
    const TrashMover silent = [](const QString &, QString *) { return true; };
    QVERIFY(sendToTrashWith(a, &where, silent) == SendResult::StillThere);
    QCOMPARE(linksTo(a), 2);

    // The library says no: not taken, and the file as it was.
    const TrashMover no = [](const QString &, QString *) { return false; };
    QVERIFY(sendToTrashWith(a, &where, no) == SendResult::NotTaken);
    const QList<TrashOutcome> out = moveFilesToTrashWith({ a }, no);
    QCOMPARE(out.at(0).reason, s(u"the trash did not take it"));
    QCOMPARE(read(a), QByteArray("a"));
}

void TrashStrayTest::whatIsNotAFileNameIsNotTried()
{
    // FD-SEND: a path that ends in "/" names a folder; it is never handed over.
    write(QStringLiteral("f/inside.flac"), "x");
    const QString folder = m_dir.filePath(QStringLiteral("f"));
    QString where;
    for (const QString &path : { folder + QLatin1Char('/'), folder, QString(), s(u"/") }) {
        QVERIFY2(sendToTrashWith(path, &where, secondName()) == SendResult::NotTaken, qPrintable(path));
        QVERIFY2(!sendToTrash(path, &where), qPrintable(path));
    }
    QCOMPARE(m_calls, 0);
    QCOMPARE(read(folder + QStringLiteral("/inside.flac")), QByteArray("x"));
    QVERIFY(!QFileInfo::exists(files()) || listed(files()).isEmpty());
}

void TrashStrayTest::theRealOne()
{
    // With the desktop library itself: it goes, and the answer is Went. A stand-in that moves gives the same.
    const QString a = write(QStringLiteral("r/a.flac"), "a");
    QString where;
    QVERIFY(sendToTrash(a, &where));
    QVERIFY(!QFileInfo::exists(a));
    QCOMPARE(read(where), QByteArray("a"));
    const QString b = write(QStringLiteral("r/b.flac"), "b");
    const TrashMover moves = [this](const QString &real, QString *place) {
        *place = files() + QStringLiteral("/b.flac");
        return QFile::rename(real, *place);
    };
    QVERIFY(sendToTrashWith(b, &where, moves) == SendResult::Went);
    QCOMPARE(where, files() + QStringLiteral("/b.flac"));
    QCOMPARE(read(where), QByteArray("b"));
    const QList<TrashOutcome> out = moveFilesToTrashWith({ write(QStringLiteral("r/c.flac"), "c") }, { });
    QVERIFY(out.at(0).status == Status::Trashed); // no stand-in given: the desktop library
}

QTEST_GUILESS_MAIN(TrashStrayTest)
#include "tst_trash_stray.moc"
