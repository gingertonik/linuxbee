// LOCKED ACCEPTANCE TEST (the organiser: a link is moved only where it still leads to its file).
// Written by the reviewer, not the builder. The builder makes these pass by changing src/, never
// by editing this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are O-EXECUTE ("A source that is a link") in docs/spec/organise.md. The fault
// this holds shut (the write-path audit, part 2, F5, 2026-11-18): a link whose text is relative
// was renamed into another folder as any file is. From there it led nowhere. The move was
// reported as done, and the library then marked the track missing.
//
// All files are made here, in a temporary folder:
//   top/root/real/x.mp3          the music
//   top/root/in/alb/l.mp3        a link to it
//   top/root/in/other, top/root/out, top/root/out/deep     folders to move it to
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <sys/stat.h>
#include <unistd.h>

#include "organise/transfer.h"

using namespace linuxbee;

namespace {

const QByteArray kMusic = QByteArrayLiteral("the music, as bytes");
const QByteArray kOther = QByteArrayLiteral("another file of the same name");
const QString kBroken = QStringLiteral("the link would not lead to its file from there");

QByteArray read(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<unreadable>");
}

bool put(const QString &path, const QByteArray &data)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
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

// The number of the file a name leads to, links followed. 0 when it leads nowhere.
quint64 leadsTo(const QString &path)
{
    struct stat st { };
    return ::stat(QFile::encodeName(path).constData(), &st) == 0 ? quint64(st.st_ino) : 0;
}

struct Box {
    QTemporaryDir dir;
    QString top, root, real, link;
    Box()
    {
        top = QFileInfo(dir.path()).canonicalFilePath();
        root = top + QStringLiteral("/root");
        for (const char *sub : { "/real", "/in/alb", "/in/other", "/out/deep" })
            QDir().mkpath(root + QLatin1String(sub));
        real = root + QStringLiteral("/real/x.mp3");
        link = root + QStringLiteral("/in/alb/l.mp3");
        if (!put(real, kMusic))
            QTest::qFail("could not write the music", __FILE__, __LINE__);
    }
    bool makeLink(const QString &text) const
    {
        return ::symlink(QFile::encodeName(text).constData(), QFile::encodeName(link).constData())
            == 0;
    }
};

const QString kRelative = QStringLiteral("../../real/x.mp3");

}

class OrganiseLinkTest : public QObject {
    Q_OBJECT
private slots:
    void aLinkThatWouldLeadNowhereIsNotMoved();
    void aLinkThatWouldLeadToAnotherFileIsNotMoved();
    void aLinkThatStillLeadsToItsFileIsMoved_data();
    void aLinkThatStillLeadsToItsFileIsMoved();
    void aLinkThatLeadsNowhereAlreadyIsMovedAsBefore();
    void aCopyReadsThroughTheLinkAsBefore();
};

void OrganiseLinkTest::aLinkThatWouldLeadNowhereIsNotMoved()
{
    // From top/root/out the text leads to top/real/x.mp3, and nothing is there. The move
    // fails and says why; the link is where it was and leads where it led; the target's name
    // is free; the music is untouched.
    Box box;
    QVERIFY(box.makeLink(kRelative));
    const QString target = box.root + QStringLiteral("/out/l.mp3");
    QCOMPARE(organise::transferFile(box.link, target, true), kBroken);
    QCOMPARE(whatIs(box.link), QStringLiteral("link ") + kRelative);
    QCOMPARE(leadsTo(box.link), leadsTo(box.real));
    QCOMPARE(whatIs(target), QStringLiteral("nothing"));
    QCOMPARE(read(box.real), kMusic);
}

void OrganiseLinkTest::aLinkThatWouldLeadToAnotherFileIsNotMoved()
{
    // From top/root/out the text leads to top/real/x.mp3, and a file is there: another one.
    // That a file is there is not enough. It is asked which file.
    Box box;
    QVERIFY(box.makeLink(kRelative));
    QVERIFY(QDir().mkpath(box.top + QStringLiteral("/real")));
    QVERIFY(put(box.top + QStringLiteral("/real/x.mp3"), kOther));
    const QString target = box.root + QStringLiteral("/out/l.mp3");
    QCOMPARE(organise::transferFile(box.link, target, true), kBroken);
    QCOMPARE(whatIs(box.link), QStringLiteral("link ") + kRelative);
    QCOMPARE(read(box.link), kMusic);
    QCOMPARE(whatIs(target), QStringLiteral("nothing"));
}

void OrganiseLinkTest::aLinkThatStillLeadsToItsFileIsMoved_data()
{
    QTest::addColumn<bool>("absolute");
    QTest::addColumn<QString>("to");
    QTest::newRow("a relative text, a folder as deep")
        << false << QStringLiteral("/out/deep/l.mp3");
    QTest::newRow("a relative text, a folder beside") << false << QStringLiteral("/in/other/l.mp3");
    QTest::newRow("a relative text, a new name in its folder")
        << false << QStringLiteral("/in/alb/m.mp3");
    QTest::newRow("an absolute text, a folder less deep") << true << QStringLiteral("/out/l.mp3");
    QTest::newRow("an absolute text, a folder as deep")
        << true << QStringLiteral("/out/deep/l.mp3");
}

void OrganiseLinkTest::aLinkThatStillLeadsToItsFileIsMoved()
{
    // As before: the link itself is renamed, its text is not changed, and it leads to the
    // music from where it now is.
    QFETCH(bool, absolute);
    QFETCH(QString, to);
    Box box;
    const QString text = absolute ? box.real : kRelative;
    QVERIFY(box.makeLink(text));
    const QString target = box.root + to;
    QCOMPARE(organise::transferFile(box.link, target, true), QString());
    QCOMPARE(whatIs(box.link), QStringLiteral("nothing"));
    QCOMPARE(whatIs(target), QStringLiteral("link ") + text);
    QCOMPARE(leadsTo(target), leadsTo(box.real));
    QCOMPARE(read(target), kMusic);
}

void OrganiseLinkTest::aLinkThatLeadsNowhereAlreadyIsMovedAsBefore()
{
    // There is no file to lose the way to. The name is moved, as any name is.
    Box box;
    const QString text = QStringLiteral("../../real/gone.mp3");
    QVERIFY(box.makeLink(text));
    const QString target = box.root + QStringLiteral("/out/l.mp3");
    QCOMPARE(organise::transferFile(box.link, target, true), QString());
    QCOMPARE(whatIs(box.link), QStringLiteral("nothing"));
    QCOMPARE(whatIs(target), QStringLiteral("link ") + text);
}

void OrganiseLinkTest::aCopyReadsThroughTheLinkAsBefore()
{
    // A Copy makes a file of the music's bytes. Where the link would lead from there is not
    // asked: nothing is moved.
    Box box;
    QVERIFY(box.makeLink(kRelative));
    const QString target = box.root + QStringLiteral("/out/l.mp3");
    QCOMPARE(organise::transferFile(box.link, target, false), QString());
    QCOMPARE(whatIs(target), QStringLiteral("file"));
    QCOMPARE(read(target), kMusic);
    QCOMPARE(whatIs(box.link), QStringLiteral("link ") + kRelative);
}

QTEST_GUILESS_MAIN(OrganiseLinkTest)
#include "tst_organise_link.moc"
