// LOCKED ACCEPTANCE TEST (covers: MusicBee's order, then linuxBee's own names). Written by the
// reviewer, not the builder. The builder makes these pass by changing src/, never by editing this
// file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Rules: docs/spec/covers.md, CV-FILE and CV-FIND. Sources beside each row:
//   W-TP  MusicBee wiki, "Tags Preferences", "set which pictures are displayed": by default
//         Cover.*, Artwork.*, Front.*, <Filename>.*, then Embedded; Folder.jpg is an option
//   CV-x  a linuxBee decision in covers.md (folder, album, albumart and the lone picture, after
//         MusicBee's order: the owner's ruling of 2026-11-20 and the orchestrator's of 2026-11-21)
// Every file is made in a temporary folder of this test's own; the music files are copies of
// fixtures.
#include <QtTest>

#include "covers/cover.h"

using linuxbee::Cover;
using linuxbee::coverFileIn;
using linuxbee::CoverOptions;
using linuxbee::findCover;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

struct Place {
    QTemporaryDir dir;
    QString root;
    Place() { root = QDir::cleanPath(QFileInfo(dir.path()).canonicalFilePath()); }
    QString path(const QString &relative) const { return root + QLatin1Char('/') + relative; }
    QString file(const QString &relative)
    {
        const QString target = path(relative);
        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile out(target);
        if (!out.open(QIODevice::WriteOnly) || out.write("picture") != 7)
            QTest::qFail("could not write a file", __FILE__, __LINE__);
        return target;
    }
    QString music(const char16_t *fixture, const QString &relative)
    {
        const QString target = path(relative);
        QDir().mkpath(QFileInfo(target).absolutePath());
        if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/") + s(fixture), target))
            QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
        return target;
    }
};

// What findCover answers for a track in a folder holding these pictures: "embedded", the picture's
// name, or "none".
QString answer(bool withPicture, const QStringList &pictures,
    const QString &trackName = s(u"01 One.mp3"), const CoverOptions &options = { })
{
    Place place;
    const QString track
        = place.music(withPicture ? u"basic.mp3" : u"v23.mp3", QStringLiteral("a/") + trackName);
    for (const QString &name : pictures)
        place.file(QStringLiteral("a/") + name);
    const Cover c = findCover(track, options);
    if (c.source == Cover::Source::Embedded)
        return s(u"embedded");
    if (c.source == Cover::Source::None)
        return s(u"none");
    return QFileInfo(c.file).fileName();
}

}

class CoversOrderTest : public QObject {
    Q_OBJECT
private slots:
    void defaults();
    void musicBeesOrder_data();
    void musicBeesOrder();
    void theFolderAlone();
    void otherOrders();
};

void CoversOrderTest::defaults()
{
    const CoverOptions o;
    QCOMPARE(
        o.order, QList<Cover::Source>({ Cover::Source::File, Cover::Source::Embedded })); // W-TP
    QVERIFY(o.fallbackLast);
    QCOMPARE(linuxbee::coverNames(),
        QStringList({ s(u"cover"), s(u"artwork"), s(u"front"), s(u"folder"), s(u"album"),
            s(u"albumart") }));
}

void CoversOrderTest::musicBeesOrder_data()
{
    QTest::addColumn<bool>("withPicture");
    QTest::addColumn<QStringList>("pictures");
    QTest::addColumn<QString>("want");
    auto row = [](const char *name, bool inside, QStringList pictures, const char16_t *want) {
        QTest::newRow(name) << inside << pictures << s(want);
    };
    // W-TP: Cover, Artwork, Front and the file's own name come before the picture inside.
    row("cover before embedded", true, { s(u"cover.jpg") }, u"cover.jpg");
    row("artwork before embedded", true, { s(u"Artwork.png") }, u"Artwork.png");
    row("front before embedded", true, { s(u"front.jpeg") }, u"front.jpeg");
    row("the file's name before embedded", true, { s(u"01 One.jpg") }, u"01 One.jpg");
    row("the file's name, case ignored", true, { s(u"01 ONE.PNG") }, u"01 ONE.PNG");
    row("cover before artwork", true, { s(u"artwork.jpg"), s(u"cover.png") }, u"cover.png");
    row("artwork before front", true, { s(u"front.jpg"), s(u"artwork.png") }, u"artwork.png");
    row("front before the file's name", true, { s(u"01 One.jpg"), s(u"front.png") }, u"front.png");
    // CV-FIND: linuxBee's own names only when MusicBee's order found nothing.
    row("embedded before folder", true, { s(u"folder.jpg") }, u"embedded");
    row("embedded before album", true, { s(u"album.jpg") }, u"embedded");
    row("embedded before albumart", true, { s(u"albumart.png") }, u"embedded");
    row("embedded before the lone picture", true, { s(u"scan.jpg") }, u"embedded");
    row("another track's name is not this one's", true, { s(u"02 Two.jpg") }, u"embedded");
    row("folder", false, { s(u"folder.jpg") }, u"folder.jpg");
    row("folder before album", false, { s(u"album.jpg"), s(u"Folder.png") }, u"Folder.png");
    row("album before albumart", false, { s(u"albumart.jpg"), s(u"album.png") }, u"album.png");
    row("albumart", false, { s(u"albumart.png"), s(u"scan.jpg") }, u"albumart.png");
    row("the lone picture", false, { s(u"scan.jpg") }, u"scan.jpg");
    row("two pictures, neither named", false, { s(u"scan1.jpg"), s(u"scan2.jpg") }, u"none");
    row("front before folder", false, { s(u"folder.jpg"), s(u"front.jpg") }, u"front.jpg");
    row("the file's name before folder", false, { s(u"folder.jpg"), s(u"01 One.png") },
        u"01 One.png");
    row("nothing", true, { }, u"embedded");
    row("nothing at all", false, { }, u"none");
}

void CoversOrderTest::musicBeesOrder()
{
    QFETCH(bool, withPicture);
    QFETCH(QStringList, pictures);
    QFETCH(QString, want);
    QCOMPARE(answer(withPicture, pictures), want);
}

void CoversOrderTest::theFolderAlone()
{
    // CV-FILE: a folder has no music file's name; its own order.
    Place place;
    place.file(s(u"a/folder.jpg"));
    place.file(s(u"a/front.jpg"));
    QCOMPARE(coverFileIn(place.path(s(u"a"))), place.path(s(u"a/front.jpg")));
    place.file(s(u"a/artwork.jpg"));
    QCOMPARE(coverFileIn(place.path(s(u"a"))), place.path(s(u"a/artwork.jpg")));
    QFile::remove(place.path(s(u"a/front.jpg")));
    QFile::remove(place.path(s(u"a/artwork.jpg")));
    QCOMPARE(coverFileIn(place.path(s(u"a"))), place.path(s(u"a/folder.jpg")));
}

void CoversOrderTest::otherOrders()
{
    // CV-FIND: the order is the user's (W-FAQ); linuxBee's names still come last.
    CoverOptions embeddedFirst;
    embeddedFirst.order = { Cover::Source::Embedded, Cover::Source::File };
    QCOMPARE(answer(true, { s(u"cover.jpg") }, s(u"01 One.mp3"), embeddedFirst), s(u"embedded"));
    QCOMPARE(answer(false, { s(u"cover.jpg"), s(u"folder.jpg") }, s(u"01 One.mp3"), embeddedFirst),
        s(u"cover.jpg"));
    QCOMPARE(
        answer(false, { s(u"folder.jpg") }, s(u"01 One.mp3"), embeddedFirst), s(u"folder.jpg"));
    // fallbackLast false: every file before the picture inside (linuxbeectl's --file-first).
    CoverOptions filesFirst;
    filesFirst.fallbackLast = false;
    QCOMPARE(answer(true, { s(u"folder.jpg") }, s(u"01 One.mp3"), filesFirst), s(u"folder.jpg"));
    QCOMPARE(answer(true, { s(u"scan.jpg") }, s(u"01 One.mp3"), filesFirst), s(u"scan.jpg"));
    QCOMPARE(answer(true, { s(u"folder.jpg"), s(u"front.jpg") }, s(u"01 One.mp3"), filesFirst),
        s(u"front.jpg"));
    // Without File in the order, no file at all, not even linuxBee's names.
    CoverOptions embeddedOnly;
    embeddedOnly.order = { Cover::Source::Embedded };
    QCOMPARE(answer(false, { s(u"folder.jpg"), s(u"cover.jpg") }, s(u"01 One.mp3"), embeddedOnly),
        s(u"none"));
    // File alone: MusicBee's names, then linuxBee's.
    CoverOptions fileOnly;
    fileOnly.order = { Cover::Source::File };
    QCOMPARE(answer(true, { s(u"album.jpg") }, s(u"01 One.mp3"), fileOnly), s(u"album.jpg"));
}

QTEST_GUILESS_MAIN(CoversOrderTest)
#include "tst_covers_order.moc"
