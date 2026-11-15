// LOCKED ACCEPTANCE TEST (covers: pictures that are not pictures). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/covers.md (CV-EMBEDDED, CV-FILE).
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <unistd.h>

#include "covers/cover.h"

using namespace linuxbee;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

Picture picture(const char16_t *type, qsizetype bytes)
{
    Picture p;
    p.type = s(type);
    p.mimeType = QStringLiteral("image/png");
    p.byteCount = bytes;
    return p;
}

const qint64 kLimit = 32 * 1024 * 1024; // CV-FILE: the largest picture file that counts

struct Place {
    QTemporaryDir dir;
    QString root;
    Place() { root = QDir::cleanPath(QFileInfo(dir.path()).canonicalFilePath()); }
    QString path(const char16_t *relative) const { return root + QLatin1Char('/') + s(relative); }
    QString file(const char16_t *relative, const QByteArray &bytes = QByteArray("picture"))
    {
        const QString target = path(relative);
        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile out(target);
        if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size())
            QTest::qFail("could not write a file", __FILE__, __LINE__);
        return target;
    }
    // A file of this size that takes no room on the disk.
    void sized(const char16_t *relative, qint64 size)
    {
        QFile f(file(relative, QByteArray()));
        if (!f.resize(size))
            QTest::qFail("could not size a file", __FILE__, __LINE__);
    }
};

CoverOptions fileOnly(bool data)
{
    CoverOptions o;
    o.order = { Cover::Source::File };
    o.data = data;
    return o;
}

}

class CoversOddTest : public QObject {
    Q_OBJECT
private slots:
    void anEmbeddedPictureOfNoBytes();
    void aFileOfNoBytes();
    void aFileTooLarge();
    void aFileThatCannotBeRead();
    void aLinkOutOfTheFolder();
};

void CoversOddTest::anEmbeddedPictureOfNoBytes()
{
    // CV-EMBEDDED: a picture of no bytes is passed over.
    QCOMPARE(chooseEmbedded({ picture(u"Front Cover", 0) }), -1);
    QCOMPARE(chooseEmbedded({ picture(u"Front Cover", 0), picture(u"Back Cover", 1) }), 1);
    QCOMPARE(chooseEmbedded({ picture(u"Other", 0), picture(u"Front Cover", 0), picture(u"Other", 5) }), 2);
    QCOMPARE(chooseEmbedded({ picture(u"Back Cover", 3), picture(u"Front Cover", 0) }), 0);
    QCOMPARE(chooseEmbedded({ picture(u"Back Cover", 0), picture(u"Other", 2), picture(u"Front Cover", 9) }), 2);
}

void CoversOddTest::aFileOfNoBytes()
{
    // CV-FILE: a file of no bytes is not a picture file, by name or as the lone picture.
    Place place;
    place.file(u"a/cover.jpg", QByteArray());
    QCOMPARE(coverFileIn(place.path(u"a")), QString());
    place.file(u"a/folder.png");
    QCOMPARE(coverFileIn(place.path(u"a")), place.path(u"a/folder.png"));
    place.file(u"b/cover.jpg", QByteArray());
    place.file(u"b/back.png");
    QCOMPARE(coverFileIn(place.path(u"b")), place.path(u"b/back.png")); // the lone picture
    place.file(u"b/cover.jpg"); // with bytes it is the cover again
    QCOMPARE(coverFileIn(place.path(u"b")), place.path(u"b/cover.jpg"));
}

void CoversOddTest::aFileTooLarge()
{
    // CV-FILE: a file of more than 32 MiB is not a picture file.
    Place place;
    place.sized(u"a/cover.jpg", kLimit + 1);
    QCOMPARE(coverFileIn(place.path(u"a")), QString());
    place.file(u"a/folder.png");
    QCOMPARE(coverFileIn(place.path(u"a")), place.path(u"a/folder.png"));
    QCOMPARE(findCover(place.path(u"a/track.mp3"), fileOnly(true)).file, place.path(u"a/folder.png"));
    place.sized(u"a/cover.jpg", kLimit); // exactly the limit counts
    QCOMPARE(coverFileIn(place.path(u"a")), place.path(u"a/cover.jpg"));
    QCOMPARE(findCover(place.path(u"a/track.mp3"), fileOnly(false)).byteCount, qsizetype(kLimit));
}

void CoversOddTest::aFileThatCannotBeRead()
{
    // CV-FILE: a file this user cannot read is not a picture file; asked with or without the bytes.
    if (geteuid() == 0)
        QSKIP("the superuser can read every file");
    Place place;
    const QString locked = place.file(u"a/cover.jpg");
    place.file(u"a/folder.png", QByteArray("the other"));
    QVERIFY(QFile::setPermissions(locked, QFileDevice::Permissions()));
    QCOMPARE(coverFileIn(place.path(u"a")), place.path(u"a/folder.png"));
    const Cover without = findCover(place.path(u"a/track.mp3"), fileOnly(false));
    const Cover with = findCover(place.path(u"a/track.mp3"), fileOnly(true));
    QCOMPARE(without.file, place.path(u"a/folder.png"));
    QCOMPARE(with.file, without.file);
    QCOMPARE(with.data, QByteArray("the other"));
    QVERIFY(QFile::setPermissions(locked, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    QCOMPARE(coverFileIn(place.path(u"a")), locked);
}

void CoversOddTest::aLinkOutOfTheFolder()
{
    // CV-FILE: a link to a regular file elsewhere is followed; the cover is named as it stands in the folder.
    Place place;
    const QString shared = place.file(u"shared/art.jpg", QByteArray("shared art"));
    QDir().mkpath(place.path(u"a"));
    QVERIFY(QFile::link(shared, place.path(u"a/cover.jpg")));
    QCOMPARE(coverFileIn(place.path(u"a")), place.path(u"a/cover.jpg"));
    const Cover c = findCover(place.path(u"a/track.mp3"), fileOnly(true));
    QCOMPARE(c.file, place.path(u"a/cover.jpg"));
    QCOMPARE(c.data, QByteArray("shared art"));
    // The limits are those of the file it leads to.
    QVERIFY(QFile::resize(shared, 0));
    QCOMPARE(coverFileIn(place.path(u"a")), QString());
}

QTEST_GUILESS_MAIN(CoversOddTest)
#include "tst_covers_odd.moc"
