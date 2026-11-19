// LOCKED ACCEPTANCE TEST (the converter: the last move through the job's three calls; what is
// "there"; what is "still that file"). Written by the reviewer, not the builder. The builder makes
// these pass by changing src/, never by editing this file. A test believed to be wrong is
// reported, with the reason, and left alone.
//
// Expected values are C-PLACE and C-EXISTS in docs/spec/convert.md. What this holds shut (the
// review of fix-95, 2026-11-19): a name taken between Keep both's look and its move, which no
// test could reach; the way of a disk that has no move that never replaces; a disk that has no
// second names either; a file that was seen and is changed, or gone, by the end; a link that
// leads nowhere, which counted as nothing.
//
// The calls of the last step are the job's own (ConvertJob::placeIo). Here each one is the real
// call with something done before it, or an answer another disk would give. The files are real:
// a call made past the job's own would be seen on the disk. The sound is made with ffmpeg;
// ffprobe is needed too (some cases replace a file, C-REPLACE). Everything is in a temporary
// folder.
#include <QProcess>
#include <QtTest>

#include <cerrno>
#include <dirent.h>
#include <sys/stat.h>

#include "convert/converter.h"

using linuxbee::ConvertJob;
using linuxbee::ConvertResult;

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

bool ffmpeg(const QStringList &arguments)
{
    QProcess p;
    p.start(QStringLiteral("ffmpeg"),
        QStringList({ s(u"-nostdin"), s(u"-v"), s(u"error"), s(u"-y") }) + arguments);
    return p.waitForFinished(120000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

const QByteArray kTheirs = QByteArrayLiteral("a file that another program put here meanwhile\n");
const char16_t *kAppeared
    = u"a file appeared at the destination while converting; it was not replaced";
const char16_t *kChanged
    = u"the file at the destination was changed while converting; it was not replaced";
const char16_t *kGone = u"the file at the destination was removed while converting; the "
                        u"converted file was not put there";
const char16_t *kNoWay = u"the converted file could not be put in place: this disk can neither "
                         u"move a file without replacing one nor give it a second name";
const char16_t *kNotMoved = u"could not move the converted file into place";

// Another program's file is put at `path`, which must be free.
bool put(const char *path)
{
    QFile file(QFile::decodeName(path));
    return file.open(QIODevice::WriteOnly | QIODevice::NewOnly)
        && file.write(kTheirs) == kTheirs.size();
}

// The answer of a call that failed with `error`. Nothing is done.
int refuse(int error)
{
    errno = error;
    return -1;
}

QString nameOf(const char *path)
{
    return QFileInfo(QFile::decodeName(path)).fileName();
}

// What the system says of the name itself: the file's number, how many names it has, and the
// time of its contents. 0 when nothing has the name.
struct Told {
    quint64 number = 0;
    quint64 names = 0;
    qint64 contents = 0;
};

Told told(const QString &path)
{
    Told answer;
    struct stat info;
    if (::lstat(QFile::encodeName(path).constData(), &info) == 0) {
        answer.number = quint64(info.st_ino);
        answer.names = quint64(info.st_nlink);
        answer.contents = qint64(info.st_mtim.tv_sec) * 1000000000 + info.st_mtim.tv_nsec;
    }
    return answer;
}

}

class ConvertPlaceIoTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void aJobHasTheRealCalls();
    void keepBothAndEachNameIsTakenAtTheMove_data();
    void keepBothAndEachNameIsTakenAtTheMove();
    void theNameIsTakenAtTheMove_data();
    void theNameIsTakenAtTheMove();
    void aDiskWithoutTheMoveThatNeverReplaces_data();
    void aDiskWithoutTheMoveThatNeverReplaces();
    void theSecondNameWayAndTheNameIsTaken_data();
    void theSecondNameWayAndTheNameIsTaken();
    void noSecondNameEither_data();
    void noSecondNameEither();
    void thePartNameCannotBeTakenAway();
    void theFileSeenWasChanged_data();
    void theFileSeenWasChanged();
    void theFileSeenIsGone();
    void aLinkThatLeadsNowhereIsThere_data();
    void aLinkThatLeadsNowhereIsThere();
    void overwriteOntoALinkToAFile();

private:
    QString f(const char16_t *name) const { return m_dir.filePath(s(name)); }
    QString w(const char16_t *name) const { return m_work + QLatin1Char('/') + s(name); }
    // An encoder that ends well, has written the four-second MP3, and has `more` done besides.
    QString racer(const QByteArray &more);
    // Every name in the folder of the case but the stand-in's, sorted.
    QStringList entries() const;
    ConvertJob job(ConvertJob::IfExists ifExists) const;
    QTemporaryDir m_dir;
    QString m_work;
    int m_counter = 0;
};

void ConvertPlaceIoTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    QVERIFY2(!QStandardPaths::findExecutable(s(u"ffprobe")).isEmpty(), "ffprobe is needed");
    QVERIFY2(ffmpeg({ s(u"-f"), s(u"lavfi"), s(u"-i"), s(u"sine=frequency=440:duration=4"),
                 s(u"-ac"), s(u"2"), f(u"four.flac") }),
        "ffmpeg is needed, with its lavfi input");
    QVERIFY(ffmpeg({ s(u"-i"), f(u"four.flac"), f(u"four.mp3") }));
    QVERIFY(ffmpeg({ s(u"-i"), f(u"four.flac"), s(u"-af"), s(u"volume=0.5"), f(u"mine.mp3") }));
}

void ConvertPlaceIoTest::init()
{
    m_work = m_dir.filePath(QStringLiteral("t%1").arg(++m_counter));
    QVERIFY(QDir().mkpath(m_work));
}

QString ConvertPlaceIoTest::racer(const QByteArray &more)
{
    const QString path = m_work + QStringLiteral("/racer.sh");
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write("#!/bin/sh\nfor a in \"$@\"; do out=$a; done\ncp '"
            + QFile::encodeName(f(u"four.mp3")) + "' \"$out\" || exit 1\n" + more + "exit 0\n");
        file.close();
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    }
    return path;
}

QStringList ConvertPlaceIoTest::entries() const
{
    QStringList names;
    if (DIR *folder = ::opendir(QFile::encodeName(m_work).constData())) {
        while (const struct dirent *entry = ::readdir(folder)) {
            const QString name = QFile::decodeName(entry->d_name);
            if (name != s(u".") && name != s(u"..") && !name.endsWith(s(u".sh")))
                names.append(name);
        }
        ::closedir(folder);
    }
    names.sort();
    return names;
}

ConvertJob ConvertPlaceIoTest::job(ConvertJob::IfExists ifExists) const
{
    ConvertJob j;
    j.source = f(u"four.flac");
    j.settings = linuxbee::encoderPreset(s(u"mp3-320"))->settings;
    j.destination = w(u"song.mp3");
    j.ifExists = ifExists;
    j.copyTags = false;
    return j;
}

void ConvertPlaceIoTest::aJobHasTheRealCalls()
{
    // A job made without calls of its own has the system's: a second name, a name taken away, a
    // move, and a move onto a name that is taken, which is refused and replaces nothing.
    const ConvertJob j;
    QVERIFY(j.placeIo.renameNoReplace && j.placeIo.link && j.placeIo.unlink);
    const QByteArray a = QFile::encodeName(w(u"a"));
    const QByteArray b = QFile::encodeName(w(u"b"));
    QVERIFY(put(a.constData()));
    QCOMPARE(j.placeIo.link(a.constData(), b.constData()), 0);
    QCOMPARE(told(w(u"b")).names, quint64(2));
    QCOMPARE(j.placeIo.unlink(a.constData()), 0);
    QCOMPARE(entries(), QStringList({ s(u"b") }));
    QCOMPARE(j.placeIo.renameNoReplace(b.constData(), a.constData()), 0);
    QCOMPARE(entries(), QStringList({ s(u"a") }));
    {
        QFile other(w(u"b"));
        QVERIFY(other.open(QIODevice::WriteOnly) && other.write("another\n") == 8);
    }
    errno = 0;
    const int answer = j.placeIo.renameNoReplace(a.constData(), b.constData());
    const int error = errno;
    QCOMPARE(answer, -1);
    QCOMPARE(error, EEXIST);
    QCOMPARE(bytesOf(w(u"a")), kTheirs);
    QCOMPARE(bytesOf(w(u"b")), QByteArrayLiteral("another\n"));
}

void ConvertPlaceIoTest::keepBothAndEachNameIsTakenAtTheMove_data()
{
    QTest::addColumn<int>("taken");
    QTest::newRow("the first free name") << 1;
    QTest::newRow("and the next") << 2;
    QTest::newRow("and the one after") << 3;
}

void ConvertPlaceIoTest::keepBothAndEachNameIsTakenAtTheMove()
{
    // C-EXISTS Rename: song.mp3 is there, so the result is to go to song_1.mp3. Another program
    // takes that name at the very moment of the move, after every look; and so with the next
    // names. None is replaced, and the result goes to the first name that is still free.
    QFETCH(int, taken);
    ConvertJob j = job(ConvertJob::IfExists::Rename);
    QVERIFY(QFile::copy(f(u"mine.mp3"), j.destination));
    const QByteArray mine = bytesOf(j.destination);
    j.ffmpegPath = racer(QByteArray());
    QStringList asked;
    const auto move = j.placeIo.renameNoReplace;
    j.placeIo.renameNoReplace = [&asked, move, taken](const char *from, const char *to) {
        asked.append(nameOf(to));
        if (asked.size() <= taken && !put(to))
            return refuse(EIO);
        return move(from, to);
    };
    const ConvertResult r = linuxbee::convertFile(j);
    QVERIFY2(r.status == ConvertResult::Status::Converted, qPrintable(r.message));
    QStringList names;
    for (int i = 1; i <= taken + 1; ++i)
        names.append(QStringLiteral("song_%1.mp3").arg(i));
    QCOMPARE(asked, names);
    for (int i = 0; i < taken; ++i)
        QCOMPARE(bytesOf(m_work + QLatin1Char('/') + names.value(i)), kTheirs);
    QCOMPARE(r.outputPath, m_work + QLatin1Char('/') + names.value(taken));
    QCOMPARE(bytesOf(r.outputPath), bytesOf(f(u"four.mp3")));
    QCOMPARE(bytesOf(j.destination), mine);
    QCOMPARE(entries(), QStringList({ s(u"song.mp3") }) + names);
}

void ConvertPlaceIoTest::theNameIsTakenAtTheMove_data()
{
    QTest::addColumn<int>("mode");
    QTest::newRow("skip") << int(ConvertJob::IfExists::Skip);
    QTest::newRow("overwrite, nothing there") << int(ConvertJob::IfExists::Overwrite);
}

void ConvertPlaceIoTest::theNameIsTakenAtTheMove()
{
    // Nothing was at the name, and nothing is there when the encoder has ended. A file comes at
    // the very moment of the move. It is not replaced.
    QFETCH(int, mode);
    ConvertJob j = job(ConvertJob::IfExists(mode));
    j.ffmpegPath = racer(QByteArray());
    int moves = 0;
    const auto move = j.placeIo.renameNoReplace;
    j.placeIo.renameNoReplace = [&moves, move](const char *from, const char *to) {
        if (++moves == 1 && !put(to))
            return refuse(EIO);
        return move(from, to);
    };
    const ConvertResult r = linuxbee::convertFile(j);
    QCOMPARE(r.status, ConvertResult::Status::Failed);
    QCOMPARE(r.message, s(kAppeared));
    QCOMPARE(moves, 1);
    QCOMPARE(bytesOf(j.destination), kTheirs);
    QCOMPARE(entries(), QStringList({ s(u"song.mp3") }));
}

void ConvertPlaceIoTest::aDiskWithoutTheMoveThatNeverReplaces_data()
{
    QTest::addColumn<int>("error");
    QTest::newRow("EINVAL") << EINVAL;
    QTest::newRow("ENOSYS") << ENOSYS;
    QTest::newRow("EOPNOTSUPP") << EOPNOTSUPP;
}

void ConvertPlaceIoTest::aDiskWithoutTheMoveThatNeverReplaces()
{
    // The disk answers that it has no move that never replaces. The part file gets the name as
    // a second name, and its own name is taken away: one file, one name, nothing left.
    QFETCH(int, error);
    ConvertJob j = job(ConvertJob::IfExists::Skip);
    j.ffmpegPath = racer(QByteArray());
    int moves = 0;
    QStringList linkedFrom;
    QStringList linkedTo;
    QStringList takenAway;
    j.placeIo.renameNoReplace = [&moves, error](const char *, const char *) {
        ++moves;
        return refuse(error);
    };
    const auto link = j.placeIo.link;
    j.placeIo.link = [&linkedFrom, &linkedTo, link](const char *from, const char *to) {
        linkedFrom.append(nameOf(from));
        linkedTo.append(nameOf(to));
        return link(from, to);
    };
    const auto unlink = j.placeIo.unlink;
    j.placeIo.unlink = [&takenAway, unlink](const char *path) {
        takenAway.append(nameOf(path));
        return unlink(path);
    };
    const ConvertResult r = linuxbee::convertFile(j);
    QVERIFY2(r.status == ConvertResult::Status::Converted, qPrintable(r.message));
    QCOMPARE(r.outputPath, j.destination);
    QCOMPARE(bytesOf(j.destination), bytesOf(f(u"four.mp3")));
    QCOMPARE(moves, 1);
    QCOMPARE(linkedTo, QStringList({ s(u"song.mp3") }));
    QVERIFY2(linkedFrom.value(0).startsWith(s(u".lbpart-")), qPrintable(linkedFrom.value(0)));
    QCOMPARE(takenAway, linkedFrom);
    QCOMPARE(told(j.destination).names, quint64(1));
    QCOMPARE(entries(), QStringList({ s(u"song.mp3") }));
}

void ConvertPlaceIoTest::theSecondNameWayAndTheNameIsTaken_data()
{
    QTest::addColumn<int>("mode");
    QTest::newRow("skip") << int(ConvertJob::IfExists::Skip);
    QTest::newRow("overwrite, nothing there") << int(ConvertJob::IfExists::Overwrite);
    QTest::newRow("keep both") << int(ConvertJob::IfExists::Rename);
}

void ConvertPlaceIoTest::theSecondNameWayAndTheNameIsTaken()
{
    // The same disk, and a file comes at the name the result is to have just before the last
    // step asks for it. The second name is refused by the system, and the file stays. Keep both
    // goes on to the next name; Skip and Overwrite fail.
    QFETCH(int, mode);
    const bool keepBoth = mode == int(ConvertJob::IfExists::Rename);
    ConvertJob j = job(ConvertJob::IfExists(mode));
    if (keepBoth)
        QVERIFY(QFile::copy(f(u"mine.mp3"), j.destination));
    const QByteArray mine = bytesOf(f(u"mine.mp3"));
    j.ffmpegPath = racer(QByteArray());
    int moves = 0;
    j.placeIo.renameNoReplace = [&moves](const char *, const char *to) {
        if (++moves == 1 && !put(to))
            return refuse(EIO);
        return refuse(EINVAL);
    };
    const ConvertResult r = linuxbee::convertFile(j);
    if (keepBoth) {
        QVERIFY2(r.status == ConvertResult::Status::Converted, qPrintable(r.message));
        QCOMPARE(r.outputPath, w(u"song_2.mp3"));
        QCOMPARE(bytesOf(r.outputPath), bytesOf(f(u"four.mp3")));
        QCOMPARE(told(r.outputPath).names, quint64(1));
        QCOMPARE(bytesOf(w(u"song_1.mp3")), kTheirs);
        QCOMPARE(bytesOf(j.destination), mine);
        QCOMPARE(moves, 2);
        QCOMPARE(entries(), QStringList({ s(u"song.mp3"), s(u"song_1.mp3"), s(u"song_2.mp3") }));
    } else {
        QCOMPARE(r.status, ConvertResult::Status::Failed);
        QCOMPARE(r.message, s(kAppeared));
        QCOMPARE(bytesOf(j.destination), kTheirs);
        QCOMPARE(moves, 1);
        QCOMPARE(entries(), QStringList({ s(u"song.mp3") }));
    }
}

void ConvertPlaceIoTest::noSecondNameEither_data()
{
    QTest::addColumn<int>("error");
    QTest::addColumn<QString>("message");
    // The disk has no second names: said so, in words of its own.
    QTest::newRow("EPERM") << EPERM << s(kNoWay);
    QTest::newRow("ENOSYS") << ENOSYS << s(kNoWay);
    QTest::newRow("EOPNOTSUPP") << EOPNOTSUPP << s(kNoWay);
    // It has them, and this one could not be made: the words of any other failure.
    QTest::newRow("EACCES") << EACCES << s(kNotMoved);
    QTest::newRow("ENOSPC") << ENOSPC << s(kNotMoved);
}

void ConvertPlaceIoTest::noSecondNameEither()
{
    // No move that never replaces, and the second name is refused too. The job fails; nothing
    // is at the name, and nothing is left. (No call that can replace is made in their stead:
    // the cases above would see it.)
    QFETCH(int, error);
    QFETCH(QString, message);
    ConvertJob j = job(ConvertJob::IfExists::Skip);
    j.ffmpegPath = racer(QByteArray());
    int links = 0;
    j.placeIo.renameNoReplace = [](const char *, const char *) { return refuse(EINVAL); };
    j.placeIo.link = [&links, error](const char *, const char *) {
        ++links;
        return refuse(error);
    };
    const ConvertResult r = linuxbee::convertFile(j);
    QCOMPARE(r.status, ConvertResult::Status::Failed);
    QCOMPARE(r.message, message);
    QCOMPARE(links, 1);
    QCOMPARE(told(j.destination).number, quint64(0));
    QCOMPARE(entries(), QStringList());
}

void ConvertPlaceIoTest::thePartNameCannotBeTakenAway()
{
    // The second name is made, and the part file's own name cannot be taken away. The result is
    // whole and has its name: Converted. The part file's name stays beside it, a second name of
    // the same file.
    ConvertJob j = job(ConvertJob::IfExists::Skip);
    j.ffmpegPath = racer(QByteArray());
    int refused = 0;
    j.placeIo.renameNoReplace = [](const char *, const char *) { return refuse(EINVAL); };
    j.placeIo.unlink = [&refused](const char *) {
        ++refused;
        return refuse(EIO);
    };
    const ConvertResult r = linuxbee::convertFile(j);
    QVERIFY2(r.status == ConvertResult::Status::Converted, qPrintable(r.message));
    QCOMPARE(r.outputPath, j.destination);
    QCOMPARE(bytesOf(j.destination), bytesOf(f(u"four.mp3")));
    QCOMPARE(refused, 1);
    const QStringList there = entries();
    QCOMPARE(there.size(), 2);
    QVERIFY2(there.value(0).startsWith(s(u".lbpart-")) && there.value(0).endsWith(s(u"-song.mp3")),
        qPrintable(there.value(0)));
    QCOMPARE(there.value(1), s(u"song.mp3"));
    QCOMPARE(told(m_work + QLatin1Char('/') + there.value(0)).number, told(j.destination).number);
}

void ConvertPlaceIoTest::theFileSeenWasChanged_data()
{
    QTest::addColumn<QByteArray>("how");
    QTest::addColumn<int>("left");
    QTest::newRow("written again in place, longer")
        << QByteArray("printf '%s\\n' 'a file that another program put here meanwhile' > DEST")
        << 0;
    QTest::newRow("some bytes written in place, and the time of the contents put back")
        << QByteArray("touch -r DEST DEST.was && dd if=/dev/zero of=DEST bs=1 count=16 "
                      "conv=notrunc 2> /dev/null && touch -r DEST.was DEST && rm DEST.was")
        << 1;
    QTest::newRow("only touched") << QByteArray("touch -d '2001-02-03 04:05:06' DEST") << 2;
}

void ConvertPlaceIoTest::theFileSeenWasChanged()
{
    // Overwrite, and a file was there. While the encoder runs another program writes into that
    // very file: the same file by its number, and no longer as it was seen (and measured,
    // C-REPLACE). It is not replaced, and the words say what happened.
    QFETCH(QByteArray, how);
    QFETCH(int, left);
    ConvertJob j = job(ConvertJob::IfExists::Overwrite);
    QVERIFY(QFile::copy(f(u"mine.mp3"), j.destination));
    const QByteArray mine = bytesOf(j.destination);
    const Told before = told(j.destination);
    how.replace("DEST", "'" + QFile::encodeName(j.destination) + "'");
    // A twentieth of a second first: a system that keeps its times by a coarse clock would
    // give a change made within the same tick the time of the look.
    j.ffmpegPath = racer("sleep 0.05\n" + how + " || exit 1\n");
    const ConvertResult r = linuxbee::convertFile(j);
    const Told after = told(j.destination);
    QCOMPARE(after.number, before.number); // what the case is about: the same file
    QByteArray want = mine;
    if (left == 0)
        want = kTheirs;
    if (left == 1) {
        want.replace(0, 16, QByteArray(16, '\0'));
        QCOMPARE(after.contents, before.contents); // only the time of the record tells
    }
    if (left != 2)
        QVERIFY(want != mine);
    QCOMPARE(r.status, ConvertResult::Status::Failed);
    QCOMPARE(r.message, s(kChanged));
    QCOMPARE(bytesOf(j.destination), want);
    QCOMPARE(entries(), QStringList({ s(u"song.mp3") }));
}

void ConvertPlaceIoTest::theFileSeenIsGone()
{
    // Overwrite, and the file that was seen is removed while the encoder runs. It was that file
    // the job was to replace: nothing is put at the name, and the words say so.
    ConvertJob j = job(ConvertJob::IfExists::Overwrite);
    QVERIFY(QFile::copy(f(u"mine.mp3"), j.destination));
    j.ffmpegPath = racer("rm '" + QFile::encodeName(j.destination) + "' || exit 1\n");
    const ConvertResult r = linuxbee::convertFile(j);
    QCOMPARE(r.status, ConvertResult::Status::Failed);
    QCOMPARE(r.message, s(kGone));
    QCOMPARE(told(j.destination).number, quint64(0));
    QCOMPARE(entries(), QStringList());
}

void ConvertPlaceIoTest::aLinkThatLeadsNowhereIsThere_data()
{
    QTest::addColumn<int>("mode");
    QTest::addColumn<int>("outcome"); // 0 left alone, 1 the result beside it, 2 the link replaced
    QTest::newRow("skip") << int(ConvertJob::IfExists::Skip) << 0;
    QTest::newRow("update tags, no tags asked for") << int(ConvertJob::IfExists::UpdateTags) << 0;
    QTest::newRow("keep both") << int(ConvertJob::IfExists::Rename) << 1;
    QTest::newRow("overwrite") << int(ConvertJob::IfExists::Overwrite) << 2;
}

void ConvertPlaceIoTest::aLinkThatLeadsNowhereIsThere()
{
    // C-EXISTS asks the name, not what it leads to. A link that leads nowhere has the name.
    QFETCH(int, mode);
    QFETCH(int, outcome);
    ConvertJob j = job(ConvertJob::IfExists(mode));
    const QString nowhere = w(u"nowhere.mp3");
    QVERIFY(QFile::link(nowhere, j.destination));
    QVERIFY(QFileInfo(j.destination).isSymLink() && !QFileInfo::exists(j.destination));
    const QString ran = m_work + s(u".ran"); // beside the folder of the case, not in it
    j.ffmpegPath = racer(": > '" + QFile::encodeName(ran) + "'\n");
    const ConvertResult r = linuxbee::convertFile(j);
    QVERIFY(!QFileInfo::exists(nowhere)); // nothing is ever written through the link
    if (outcome == 0) {
        QCOMPARE(r.status, ConvertResult::Status::Skipped);
        QCOMPARE(r.outputPath, j.destination);
        QVERIFY2(!QFileInfo::exists(ran), "the encoder was run");
    } else {
        QVERIFY2(r.status == ConvertResult::Status::Converted, qPrintable(r.message));
        QCOMPARE(r.outputPath, outcome == 1 ? w(u"song_1.mp3") : j.destination);
        QCOMPARE(bytesOf(r.outputPath), bytesOf(f(u"four.mp3")));
    }
    if (outcome == 2) {
        QVERIFY(!QFileInfo(j.destination).isSymLink());
    } else {
        QVERIFY(QFileInfo(j.destination).isSymLink());
        QCOMPARE(QFileInfo(j.destination).symLinkTarget(), nowhere);
    }
    QStringList want { s(u"song.mp3") };
    if (outcome == 1)
        want.append(s(u"song_1.mp3"));
    QCOMPARE(entries(), want);
}

void ConvertPlaceIoTest::overwriteOntoALinkToAFile()
{
    // As it was before: Overwrite onto a link that leads to a file replaces the link itself.
    // The file it led to is as it was.
    ConvertJob j = job(ConvertJob::IfExists::Overwrite);
    const QString kept = m_work + s(u".kept.mp3"); // beside the folder of the case
    QVERIFY(QFile::copy(f(u"mine.mp3"), kept));
    const QByteArray mine = bytesOf(kept);
    QVERIFY(QFile::link(kept, j.destination));
    j.ffmpegPath = racer(QByteArray());
    const ConvertResult r = linuxbee::convertFile(j);
    QVERIFY2(r.status == ConvertResult::Status::Converted, qPrintable(r.message));
    QCOMPARE(r.outputPath, j.destination);
    QVERIFY(!QFileInfo(j.destination).isSymLink());
    QCOMPARE(bytesOf(j.destination), bytesOf(f(u"four.mp3")));
    QCOMPARE(bytesOf(kept), mine);
    QCOMPARE(entries(), QStringList({ s(u"song.mp3") }));
}

QTEST_GUILESS_MAIN(ConvertPlaceIoTest)
#include "tst_convert_placeio.moc"
