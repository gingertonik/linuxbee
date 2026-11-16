// LOCKED ACCEPTANCE TEST (the cover cache). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/covers.md (CC-x). The pictures are
// made here, in a temporary folder. Runs on the offscreen platform.
#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QMutex>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

#include "covers/covercache.h"

using namespace linuxbee::ui;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

const int kPatience = 5000;

// Albums as folders: a music file's path (the file need not be there) and, for some, a cover.
struct Albums {
    QTemporaryDir dir;
    // Makes the folder and a cover of this size and colour in it; returns the music file's path.
    QString with(const QString &album, const QColor &colour, int width = 200, int height = 200, const char *name = "cover.png")
    {
        const QString folder = dir.path() + QLatin1Char('/') + album;
        QDir().mkpath(folder);
        QImage picture(width, height, QImage::Format_RGB32);
        picture.fill(colour);
        if (!picture.save(folder + QLatin1Char('/') + QLatin1String(name)))
            QTest::qFail("could not write a picture", __FILE__, __LINE__);
        return folder + QStringLiteral("/01.flac");
    }
    QString without(const QString &album)
    {
        QDir().mkpath(dir.path() + QLatin1Char('/') + album);
        return dir.path() + QLatin1Char('/') + album + QStringLiteral("/01.flac");
    }
};

// A loader that can be held: each load takes one permit before it looks at its file.
struct Gate {
    QSemaphore go;
    QMutex mutex;
    QStringList seen; // the files, in the order their loads began
    QList<bool> onWindowThread;
    CoverCacheHooks hooks(bool held)
    {
        CoverCacheHooks h;
        h.loading = [this, held](const QString &file) {
            {
                const QMutexLocker lock(&mutex);
                seen.append(file);
                onWindowThread.append(QThread::currentThread() == qApp->thread());
            }
            if (held && !go.tryAcquire(1, 10000))
                qWarning("the test never let this load go on");
        };
        return h;
    }
    QStringList files()
    {
        const QMutexLocker lock(&mutex);
        return seen;
    }
};

QColor middle(const QImage &image)
{
    return image.pixelColor(image.width() / 2, image.height() / 2);
}

#define NEED_CACHE(c) \
    do { \
        if ((c).limit() == 0) \
            QFAIL("the cover cache has not been built"); \
    } while (false)

}

class CoverCacheTest : public QObject {
    Q_OBJECT
private slots:
    void askedOnceKeptAfter();
    void offTheWindowThread();
    void scaled();
    void theStamp();
    void atMostTheLimit();
    void waitingRequests();
    void goneWhileLoading();
};

void CoverCacheTest::askedOnceKeptAfter()
{
    // CC-ASK: the first call asks and answers nothing; when loaded, it is kept and costs no load again.
    Albums a;
    const QString red = a.with(s(u"Red"), Qt::red);
    const QString none = a.without(s(u"Bare"));
    CoverCache cache;
    NEED_CACHE(cache);
    QCOMPARE(cache.edge(), 64);
    QCOMPARE(cache.limit(), 512);
    QCOMPARE(cache.waitingLimit(), 64);
    QSignalSpy loaded(&cache, &CoverCache::loaded);

    QVERIFY(cache.cover(red).isNull()); // not read inside the call
    QVERIFY(!cache.has(red));
    QCOMPARE(loaded.count(), 0);
    QVERIFY(cache.cover(red).isNull()); // asked again while under way: no second load
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, kPatience);
    QCOMPARE(loaded.at(0).at(0).toString(), red);
    QCOMPARE(cache.loads(), 1);
    QVERIFY(cache.has(red));
    const QImage picture = cache.cover(red);
    QVERIFY(!picture.isNull());
    QCOMPARE(middle(picture), QColor(Qt::red));
    QCOMPARE(cache.size(), 1);
    for (int i = 0; i < 5; ++i)
        QVERIFY(!cache.cover(red).isNull());
    QTest::qWait(50);
    QCOMPARE(cache.loads(), 1); // kept: asking costs nothing
    QCOMPARE(loaded.count(), 1);

    // No cover: that is kept too, as a null image, and is not looked for again.
    QVERIFY(cache.cover(none).isNull());
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 2, kPatience);
    QCOMPARE(loaded.at(1).at(0).toString(), none);
    QVERIFY(cache.has(none));
    QVERIFY(cache.cover(none).isNull());
    QTest::qWait(50);
    QCOMPARE(cache.loads(), 2);
    QCOMPARE(cache.size(), 2);

    // A file that is not a picture, under a cover's name: no cover.
    const QString junk = a.without(s(u"Junk"));
    QFile bad(QFileInfo(junk).path() + s(u"/cover.jpg"));
    QVERIFY(bad.open(QIODevice::WriteOnly) && bad.write("not a picture") > 0);
    bad.close();
    QVERIFY(cache.cover(junk).isNull());
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 3, kPatience);
    QVERIFY(cache.has(junk));
    QVERIFY(cache.cover(junk).isNull());
    QCOMPARE(cache.loads(), 3);
}

void CoverCacheTest::offTheWindowThread()
{
    // CC-THREAD: files are looked at on the loading thread, one at a time, in the order asked.
    Albums a;
    QStringList files;
    for (int i = 0; i < 6; ++i)
        files.append(a.with(QStringLiteral("A%1").arg(i), QColor(10 * i, 0, 0)));
    Gate gate;
    CoverCache cache(64, gate.hooks(false));
    NEED_CACHE(cache);
    QSignalSpy loaded(&cache, &CoverCache::loaded);
    QList<bool> signalOnWindowThread;
    QObject::connect(&cache, &CoverCache::loaded, &cache, [&] { signalOnWindowThread.append(QThread::currentThread() == qApp->thread()); }, Qt::DirectConnection);
    for (const QString &file : std::as_const(files))
        QVERIFY(cache.cover(file).isNull());
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 6, kPatience);
    QCOMPARE(gate.files(), files);
    QCOMPARE(gate.onWindowThread, QList<bool>(6, false));
    QCOMPARE(signalOnWindowThread, QList<bool>(6, true)); // the signal is the cache's own thread's
    for (int i = 0; i < 6; ++i)
        QCOMPARE(middle(cache.cover(files.at(i))), QColor(10 * i, 0, 0)); // each file its own picture
    QCOMPARE(cache.loads(), 6);
}

void CoverCacheTest::scaled()
{
    // CC-KEEP: a picture is kept scaled down to fit the edge, its shape kept; a small one is left as it is.
    Albums a;
    const QString wide = a.with(s(u"Wide"), Qt::blue, 400, 200);
    const QString tall = a.with(s(u"Tall"), Qt::green, 100, 300, "folder.jpg");
    const QString small = a.with(s(u"Small"), Qt::yellow, 20, 10);
    const QString exact = a.with(s(u"Exact"), Qt::cyan, 48, 48);
    CoverCache cache(48);
    NEED_CACHE(cache);
    QCOMPARE(cache.edge(), 48);
    QSignalSpy loaded(&cache, &CoverCache::loaded);
    for (const QString &file : { wide, tall, small, exact })
        cache.cover(file);
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 4, kPatience);
    QCOMPARE(cache.cover(wide).size(), QSize(48, 24));
    QCOMPARE(cache.cover(tall).size(), QSize(16, 48));
    QCOMPARE(cache.cover(small).size(), QSize(20, 10));
    QCOMPARE(cache.cover(exact).size(), QSize(48, 48));
    QCOMPARE(middle(cache.cover(wide)), QColor(Qt::blue));
    const QColor green = middle(cache.cover(tall)); // a JPEG: nearly the colour
    QVERIFY2(green.green() > 200 && green.red() < 60 && green.blue() < 60, qPrintable(green.name()));
    // An edge below 1 counts as 1.
    CoverCache tiny(0);
    QCOMPARE(tiny.edge(), 1);
}

void CoverCacheTest::theStamp()
{
    // CC-ASK: a file asked for under another stamp is loaded again; under the same one it is not.
    Albums a;
    const QString file = a.with(s(u"Album"), Qt::red);
    CoverCache cache;
    NEED_CACHE(cache);
    QSignalSpy loaded(&cache, &CoverCache::loaded);
    cache.cover(file, 100);
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, kPatience);
    QVERIFY(cache.has(file, 100));
    QVERIFY(!cache.has(file, 101));
    QVERIFY(!cache.has(file)); // stamp 0 is a stamp like any other
    QCOMPARE(middle(cache.cover(file, 100)), QColor(Qt::red));
    QCOMPARE(cache.loads(), 1);

    a.with(s(u"Album"), Qt::blue); // the cover was replaced, and the caller knows the file changed
    QVERIFY(cache.cover(file, 101).isNull()); // not the old picture
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 2, kPatience);
    QCOMPARE(middle(cache.cover(file, 101)), QColor(Qt::blue));
    QCOMPARE(cache.loads(), 2);
    QCOMPARE(cache.size(), 1); // one entry for a file: the old stamp's is gone
    QVERIFY(!cache.has(file, 100));
}

void CoverCacheTest::atMostTheLimit()
{
    // CC-KEEP: at most limit() entries; the one asked for longest ago goes first.
    Albums a;
    QStringList files;
    for (int i = 0; i < 4; ++i)
        files.append(a.with(QStringLiteral("L%1").arg(i), QColor(0, 50 * i, 0)));
    CoverCache cache;
    NEED_CACHE(cache);
    cache.setLimit(0);
    QCOMPARE(cache.limit(), 1);
    cache.setLimit(2);
    QSignalSpy loaded(&cache, &CoverCache::loaded);
    const auto load = [&](int i) {
        const int before = int(loaded.count());
        cache.cover(files.at(i));
        return QTest::qWaitFor([&] { return loaded.count() == before + 1; }, kPatience);
    };
    QVERIFY(load(0));
    QVERIFY(load(1));
    QCOMPARE(cache.size(), 2);
    QVERIFY(!cache.cover(files.at(0)).isNull()); // 0 is used: 1 is now the one unused longest
    QVERIFY(load(2));
    QCOMPARE(cache.size(), 2);
    QVERIFY(cache.has(files.at(0)));
    QVERIFY(!cache.has(files.at(1)));
    QVERIFY(cache.has(files.at(2)));
    QCOMPARE(cache.loads(), 3);
    // has() is a look, not a use: 0 was used before 2, so 0 goes next.
    QVERIFY(cache.has(files.at(0)));
    QVERIFY(load(3));
    QVERIFY(!cache.has(files.at(0)));
    QVERIFY(cache.has(files.at(2)));
    QVERIFY(cache.has(files.at(3)));
    // What went is loaded again when asked for.
    QVERIFY(load(1));
    QCOMPARE(cache.loads(), 5);
    // A smaller limit lets go at once.
    cache.setLimit(1);
    QCOMPARE(cache.size(), 1);
    QVERIFY(cache.has(files.at(1)));
}

void CoverCacheTest::waitingRequests()
{
    // CC-ASK: at most waitingLimit() requests wait; the one that waited longest is dropped for a new one.
    Albums a;
    QStringList files;
    for (int i = 0; i < 5; ++i)
        files.append(a.with(QStringLiteral("W%1").arg(i), QColor(0, 0, 50 * i)));
    Gate gate;
    CoverCache cache(64, gate.hooks(true));
    NEED_CACHE(cache);
    cache.setWaitingLimit(0);
    QCOMPARE(cache.waitingLimit(), 1);
    cache.setWaitingLimit(2);
    QSignalSpy loaded(&cache, &CoverCache::loaded);
    cache.cover(files.at(0));
    QTRY_COMPARE_WITH_TIMEOUT(gate.files().size(), 1, kPatience); // its load has begun and is held
    QCOMPARE(cache.waiting(), 0);
    QVERIFY(cache.cover(files.at(0)).isNull()); // asked again while its load is under way: no second request
    QCOMPARE(cache.waiting(), 0);
    cache.cover(files.at(1));
    cache.cover(files.at(2));
    QCOMPARE(cache.waiting(), 2);
    cache.cover(files.at(1)); // asked again while waiting: still one request, and it keeps its place
    QCOMPARE(cache.waiting(), 2);
    cache.cover(files.at(3)); // one too many: 1 waited longest and is dropped
    QCOMPARE(cache.waiting(), 2);
    cache.cover(files.at(4)); // and now 2
    QCOMPARE(cache.waiting(), 2);
    QCOMPARE(cache.loads(), 1);
    gate.go.release(10);
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 3, kPatience);
    QTest::qWait(50);
    QCOMPARE(loaded.count(), 3);
    QCOMPARE(gate.files(), QStringList({ files.at(0), files.at(3), files.at(4) }));
    QCOMPARE(cache.loads(), 3);
    QCOMPARE(cache.waiting(), 0);
    QVERIFY(!cache.has(files.at(1)));
    // A dropped request is not remembered: asking again loads it.
    cache.cover(files.at(1));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 4, kPatience);
    QVERIFY(cache.has(files.at(1)));
}

void CoverCacheTest::goneWhileLoading()
{
    // CC-THREAD: destroying the cache waits for the load under way, drops the rest, and emits nothing more.
    Albums a;
    QStringList files;
    for (int i = 0; i < 3; ++i)
        files.append(a.with(QStringLiteral("G%1").arg(i), Qt::red));
    Gate gate;
    int heard = 0;
    QObject listener;
    QThread *releaser = nullptr;
    {
        CoverCache cache(64, gate.hooks(true));
        NEED_CACHE(cache);
        QObject::connect(&cache, &CoverCache::loaded, &listener, [&heard] { ++heard; });
        for (const QString &file : std::as_const(files))
            cache.cover(file);
        QTRY_COMPARE_WITH_TIMEOUT(gate.files().size(), 1, kPatience);
        // The held load is let go from a third thread, while the cache is being destroyed.
        releaser = QThread::create([&gate] {
            QThread::msleep(200);
            gate.go.release(10);
        });
        releaser->start();
    }
    releaser->wait();
    delete releaser;
    QTest::qWait(100);
    QCOMPARE(heard, 0);
    QCOMPARE(gate.files().size(), 1); // the two that waited never began
}

QTEST_MAIN(CoverCacheTest)
#include "tst_ui_covercache.moc"
