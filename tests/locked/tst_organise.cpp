// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Where each expected value comes from:
//   GEN   the tags tests/data/tags/make_fixtures.py wrote into the fixtures
//   W-O   MusicBee wiki, "File Organization"
//   W-H   MusicBee wiki, "How to Create a File-Naming Template"
//   O-x   a rule in docs/spec/organise.md
//   P-x, F-x   rules in docs/spec/paths.md and docs/spec/track-fields.md
// Every test works in its own temporary folder on copies of the fixtures.
#include <QtTest>

#include "files/musicfiles.h"
#include "organise/organiser.h"

using linuxbee::MoveOutcome;
using linuxbee::OrganiseOptions;
using linuxbee::OrganiseReport;
using linuxbee::PlannedMove;
using Status = linuxbee::PlannedMove::Status;

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

QByteArray fixtureBytes(const char *name)
{
    return bytesOf(QStringLiteral(LINUXBEE_TEST_DATA "/tags/") + QLatin1String(name));
}

// list.at(i) that cannot run off the end: a short list gives a default entry,
// and the comparison that follows fails in the ordinary way.
template <typename List> const typename List::value_type &at(const List &list, qsizetype i)
{
    static const typename List::value_type none {};
    return i < list.size() ? list.at(i) : none;
}

// GEN + W-H + F-DISCTRACK: where the default template puts each fixture.
const QString kMp3 = QStringLiteral("Adele/21/01-11 Someone like You.mp3");
const QString kFlac = QStringLiteral("Various Artists/Flac Album/02-05 Flac Title.flac");
const QString kM4a = QStringLiteral("M4A Album Artist/M4A Album/01-03 M4A Title.m4a");

}

class OrganiseTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void musicExtensions();
    void findFiles();
    void plan();
    void planWritesNothing();
    void planTargets();
    void planConflicts();
    void move();
    void copy();
    void replace();
    void changedSincePlanning();
    void otherFiles();
    void emptyFolders();
    void progressAndCancel();
    void failure();

private:
    QString put(const char *fixture, const QString &relative);
    QString touch(const QString &relative, const QByteArray &content = "x");
    QStringList tree();
    OrganiseOptions options();
    QTemporaryDir m_dir;
    QString m_work; // holds in/ and out/
    int m_counter = 0;
};

void OrganiseTest::init()
{
    QVERIFY(m_dir.isValid());
    m_work = m_dir.filePath(QStringLiteral("t%1").arg(++m_counter));
    QVERIFY(QDir().mkpath(m_work + QStringLiteral("/in")));
    QVERIFY(QDir().mkpath(m_work + QStringLiteral("/out")));
    // Keep any trashed file inside the temporary folder where the platform allows.
    qputenv("XDG_DATA_HOME", QFile::encodeName(m_dir.filePath(QStringLiteral("xdg-data"))));
}

// Copies a fixture to m_work/relative and returns the absolute path.
QString OrganiseTest::put(const char *fixture, const QString &relative)
{
    const QString target = m_work + QLatin1Char('/') + relative;
    QDir().mkpath(QFileInfo(target).absolutePath());
    QFile::remove(target);
    if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/") + QLatin1String(fixture), target))
        return { };
    QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner);
    return target;
}

QString OrganiseTest::touch(const QString &relative, const QByteArray &content)
{
    const QString target = m_work + QLatin1Char('/') + relative;
    QDir().mkpath(QFileInfo(target).absolutePath());
    QFile file(target);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(content);
    return target;
}

// Every file and every empty folder under m_work, relative, sorted. Folders end in '/'.
QStringList OrganiseTest::tree()
{
    QStringList names;
    QDirIterator it(m_work, QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info(path);
        if (info.isDir()) {
            if (QDir(path).isEmpty(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot))
                names.append(QDir(m_work).relativeFilePath(path) + QLatin1Char('/'));
        } else {
            names.append(QDir(m_work).relativeFilePath(path));
        }
    }
    names.sort();
    return names;
}

OrganiseOptions OrganiseTest::options()
{
    OrganiseOptions o;
    o.targetRoot = m_work + QStringLiteral("/out");
    o.replaced = OrganiseOptions::Replaced::Delete;
    return o;
}

void OrganiseTest::musicExtensions()
{
    // O-MUSIC
    for (const char *name : { "a.mp3", "a.MP3", "a.flac", "a.ogg", "a.oga", "a.opus", "a.m4a", "a.m4b",
             "a.mp4", "a.aac", "a.wav", "a.wv", "a.ape", "a.mpc", "a.wma", "a.aiff", "a.aif", "a.tta",
             "a.dsf", "/x/y.z/T.N.T.Flac" })
        QVERIFY2(linuxbee::hasMusicExtension(QLatin1String(name)), name);
    for (const char *name : { "a.jpg", "a.txt", "a.cue", "a.log", "a.m3u", "mp3", ".mp3", "a.mp3.bak",
             "a", "", "/x.mp3/readme" })
        QVERIFY2(!linuxbee::hasMusicExtension(QLatin1String(name)), name);
}

void OrganiseTest::findFiles()
{
    put("basic.mp3", s(u"in/b/two.mp3"));
    put("basic.flac", s(u"in/a/one.flac"));
    put("basic.ogg", s(u"in/top.ogg"));
    put("basic.ogg", s(u"in/.hidden/secret.ogg"));
    put("basic.ogg", s(u"in/a/.hidden.ogg"));
    touch(s(u"in/a/cover.jpg"));
    // O-MUSIC: absolute, ascending, hidden things skipped.
    QCOMPARE(linuxbee::findMusicFiles(m_work + s(u"/in")),
        QStringList({ m_work + s(u"/in/a/one.flac"), m_work + s(u"/in/b/two.mp3"), m_work + s(u"/in/top.ogg") }));
    QCOMPARE(linuxbee::findMusicFiles(m_work + s(u"/in"), false), QStringList({ m_work + s(u"/in/top.ogg") }));
    QCOMPARE(linuxbee::findMusicFiles(m_work + s(u"/nope")), QStringList());
    QCOMPARE(linuxbee::findMusicFiles(QString()), QStringList());
}

void OrganiseTest::plan()
{
    const QStringList files = { put("basic.mp3", s(u"in/a/x.mp3")), put("basic.flac", s(u"in/a/y.flac")),
        put("basic.m4a", s(u"in/b/z.m4a")), put("basic.ogg", s(u"in/b/o.ogg")),
        put("basic.wv", s(u"in/b/w.wv")), put("notaudio.mp3", s(u"in/b/n.mp3")),
        m_work + s(u"/in/nope.mp3") };
    const QList<PlannedMove> p = linuxbee::planOrganise(files, options());
    QCOMPARE(p.size(), 7); // O-PLAN: one entry per input, in order
    const QString out = m_work + s(u"/out/");
    for (int i = 0; i < 7; ++i)
        QCOMPARE(at(p, i).source, files.at(i));
    QVERIFY(at(p, 0).status == Status::Ready);
    QCOMPARE(at(p, 0).target, out + kMp3);
    QVERIFY(!at(p, 0).renamed && !at(p, 0).replaces);
    QCOMPARE(at(p, 1).target, out + kFlac);
    QCOMPARE(at(p, 2).target, out + kM4a);
    // W-O: a file missing a naming tag is not organised. GEN: basic.ogg has
    // neither an album artist nor an album; basic.wv has no album artist.
    QVERIFY(at(p, 3).status == Status::MissingTags);
    QCOMPARE(at(p, 3).missingFields, QStringList({ s(u"Album Artist"), s(u"Album") }));
    QVERIFY(at(p, 3).target.isEmpty());
    QVERIFY(at(p, 4).status == Status::MissingTags);
    QCOMPARE(at(p, 4).missingFields, QStringList({ s(u"Album Artist") }));
    QVERIFY(at(p, 5).status == Status::NotAudio);
    QVERIFY(at(p, 6).status == Status::NotFound);

    // W-O: with the option off, a missing tag becomes "Unknown X".
    OrganiseOptions allow = options();
    allow.pathRules.skipWhenTagsMissing = false;
    const QList<PlannedMove> q = linuxbee::planOrganise({ files.at(3) }, allow);
    QVERIFY(at(q, 0).status == Status::Ready);
    QCOMPARE(at(q, 0).target, out + s(u"Unknown Album Artist/Unknown Album/01-04 Ogg Title.ogg"));

    // An invalid template stops every file.
    OrganiseOptions bad = options();
    bad.namingTemplate = linuxbee::Template::parse(s(u"$Nope(x)"));
    for (const PlannedMove &m : linuxbee::planOrganise(files, bad))
        QVERIFY(m.status == Status::InvalidTemplate);
    // P-EMPTY
    OrganiseOptions empty = options();
    empty.namingTemplate = linuxbee::Template::parse(s(u"..."));
    QVERIFY(at(linuxbee::planOrganise({ files.at(0) }, empty), 0).status == Status::EmptyName);
    QCOMPARE(linuxbee::planOrganise({ }, options()).size(), 0);
}

void OrganiseTest::planWritesNothing()
{
    const QStringList files = { put("basic.mp3", s(u"in/a/x.mp3")), put("basic.flac", s(u"in/a/y.flac")) };
    touch(s(u"in/a/cover.jpg"));
    const QStringList before = tree();
    OrganiseOptions o = options();
    o.moveOtherFiles = true;
    linuxbee::planOrganise(files, o);
    QCOMPARE(tree(), before); // O-PLAN: no folder is created, nothing moves
    QCOMPARE(bytesOf(files.at(0)), fixtureBytes("basic.mp3"));
}

void OrganiseTest::planTargets()
{
    // O-TARGET: no root. The file keeps its folder and takes the template's file name.
    const QString a = put("basic.mp3", s(u"in/a/x.mp3"));
    OrganiseOptions o = options();
    o.targetRoot.clear();
    QList<PlannedMove> p = linuxbee::planOrganise({ a }, o);
    QCOMPARE(at(p, 0).target, m_work + s(u"/in/a/01-11 Someone like You.mp3"));

    // O-TARGET: no template. The file keeps its name and goes straight into the root.
    o = options();
    o.useTemplate = false;
    p = linuxbee::planOrganise({ a }, o);
    QCOMPARE(at(p, 0).target, m_work + s(u"/out/x.mp3"));
    // Neither: nothing to do.
    o.targetRoot.clear();
    QVERIFY(at(linuxbee::planOrganise({ a }, o), 0).status == Status::AlreadyInPlace);

    // O-PLAN: a file that is already where the template puts it.
    const QString placed = put("basic.mp3", s(u"out/") + kMp3);
    p = linuxbee::planOrganise({ placed }, options());
    QVERIFY(at(p, 0).status == Status::AlreadyInPlace);
    QCOMPARE(at(p, 0).target, placed);
    // Paths are cleaned before they are compared.
    p = linuxbee::planOrganise({ m_work + s(u"/out/./Adele/../") + kMp3 }, options());
    QVERIFY(at(p, 0).status == Status::AlreadyInPlace);
    QCOMPARE(at(p, 0).source, placed);

    // The extension is the file's own, case kept (F-FILE, P-EXT).
    const QString upper = put("basic.mp3", s(u"in/a/UP.MP3"));
    p = linuxbee::planOrganise({ upper }, options());
    QCOMPARE(at(p, 0).target, m_work + s(u"/out/Adele/21/01-11 Someone like You.MP3"));
}

void OrganiseTest::planConflicts()
{
    const QString out = m_work + s(u"/out/Adele/21/01-11 Someone like You");
    // O-CONFLICT within one run: W-O "_#", numbered from 1 (P-DUP).
    const QStringList three = { put("basic.mp3", s(u"in/a/x.mp3")), put("basic.mp3", s(u"in/b/x.mp3")),
        put("basic.mp3", s(u"in/c/x.mp3")) };
    QList<PlannedMove> p = linuxbee::planOrganise(three, options());
    QCOMPARE(at(p, 0).target, out + s(u".mp3"));
    QVERIFY(!at(p, 0).renamed);
    QCOMPARE(at(p, 1).target, out + s(u"_1.mp3"));
    QVERIFY(at(p, 1).renamed && at(p, 1).status == Status::Ready);
    QCOMPARE(at(p, 2).target, out + s(u"_2.mp3"));

    // A file already on disk at the target.
    touch(s(u"out/Adele/21/01-11 Someone like You.mp3"), "old");
    p = linuxbee::planOrganise(three, options());
    QCOMPARE(at(p, 0).target, out + s(u"_1.mp3"));
    QVERIFY(at(p, 0).renamed && !at(p, 0).replaces);
    QCOMPARE(at(p, 1).target, out + s(u"_2.mp3"));

    // W-O "allow existing files to be replaced": the first newcomer replaces
    // it; the others still cannot replace each other.
    OrganiseOptions replace = options();
    replace.replaceExisting = true;
    p = linuxbee::planOrganise(three, replace);
    QCOMPARE(at(p, 0).target, out + s(u".mp3"));
    QVERIFY(at(p, 0).replaces && !at(p, 0).renamed);
    QCOMPARE(at(p, 1).target, out + s(u"_1.mp3"));
    QVERIFY(at(p, 1).renamed && !at(p, 1).replaces);

    // O-CONFLICT: a file that sits at its own numbered name is in place.
    const QString numbered = put("basic.mp3", s(u"out/Adele/21/01-11 Someone like You_1.mp3"));
    p = linuxbee::planOrganise({ numbered }, options());
    QVERIFY(at(p, 0).status == Status::AlreadyInPlace);
}

void OrganiseTest::move()
{
    const QStringList files = { put("basic.mp3", s(u"in/a/x.mp3")), put("basic.flac", s(u"in/a/y.flac")),
        put("basic.m4a", s(u"in/b/deep/z.m4a")), put("basic.ogg", s(u"in/c/o.ogg")) };
    const OrganiseOptions o = options();
    const QList<PlannedMove> plan = linuxbee::planOrganise(files, o);
    const OrganiseReport r = linuxbee::executeOrganise(plan, o);

    // O-EXECUTE, O-EMPTY: in/a and in/b/deep and in/b are gone; in/c still
    // holds the file that was skipped.
    QCOMPARE(tree(), QStringList({ s(u"in/c/o.ogg"), s(u"out/") + kMp3, s(u"out/") + kM4a, s(u"out/") + kFlac }));
    QCOMPARE(bytesOf(m_work + s(u"/out/") + kMp3), fixtureBytes("basic.mp3"));
    QCOMPARE(bytesOf(m_work + s(u"/out/") + kFlac), fixtureBytes("basic.flac"));
    QCOMPARE(r.outcomes.size(), 4);
    for (int i = 0; i < 3; ++i) {
        QVERIFY(at(r.outcomes, i).status == MoveOutcome::Status::Done);
        QCOMPARE(at(r.outcomes, i).source, at(plan, i).source);
        QCOMPARE(at(r.outcomes, i).target, at(plan, i).target);
        QVERIFY(!at(r.outcomes, i).otherFile);
    }
    QVERIFY(at(r.outcomes, 3).status == MoveOutcome::Status::Skipped);
    QCOMPARE(at(r.outcomes, 3).message, s(u"missing tags: Album Artist, Album"));
    QCOMPARE(r.done, 3);
    QCOMPARE(r.skipped, 1);
    QCOMPARE(r.failed, 0);
    QVERIFY(!r.cancelled);
    QCOMPARE(r.removedFolders,
        QStringList({ m_work + s(u"/in/a"), m_work + s(u"/in/b/deep"), m_work + s(u"/in/b") }));

    // Running the same plan's files again finds everything in place.
    const QList<PlannedMove> again = linuxbee::planOrganise({ m_work + s(u"/out/") + kMp3 }, o);
    QVERIFY(at(again, 0).status == Status::AlreadyInPlace);
    const OrganiseReport r2 = linuxbee::executeOrganise(again, o);
    QCOMPARE(r2.skipped, 1);
    QCOMPARE(at(r2.outcomes, 0).message, s(u"already in place"));
}

void OrganiseTest::copy()
{
    const QString a = put("basic.mp3", s(u"in/a/x.mp3"));
    const QDateTime old = QDateTime(QDate(2001, 2, 3), QTime(4, 5, 6), QTimeZone::UTC);
    {
        QFile file(a);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(old, QFileDevice::FileModificationTime));
    }
    touch(s(u"in/a/cover.jpg"));
    OrganiseOptions o = options();
    o.mode = OrganiseOptions::Mode::Copy;
    o.moveOtherFiles = true; // O-OTHER and O-EMPTY apply to moves only
    const OrganiseReport r = linuxbee::executeOrganise(linuxbee::planOrganise({ a }, o), o);
    QCOMPARE(r.done, 1);
    QCOMPARE(tree(), QStringList({ s(u"in/a/cover.jpg"), s(u"in/a/x.mp3"), s(u"out/") + kMp3 }));
    QCOMPARE(bytesOf(m_work + s(u"/out/") + kMp3), fixtureBytes("basic.mp3"));
    // O-EXECUTE: a copy keeps the modification time.
    QCOMPARE(QFileInfo(m_work + s(u"/out/") + kMp3).lastModified(QTimeZone::UTC), old);
    QVERIFY(r.removedFolders.isEmpty());

    // O-PLAN: copying again. The source does not leave, so the copy is a newcomer.
    const QList<PlannedMove> p = linuxbee::planOrganise({ a }, o);
    QVERIFY(at(p, 0).renamed);
    QCOMPARE(at(p, 0).target, m_work + s(u"/out/Adele/21/01-11 Someone like You_1.mp3"));
}

void OrganiseTest::replace()
{
    for (const auto mode : { OrganiseOptions::Replaced::Delete, OrganiseOptions::Replaced::MoveToTrash }) {
        const QString a = put("basic.mp3", s(u"in/a/x.mp3"));
        const QString target = touch(s(u"out/") + kMp3, "old");
        OrganiseOptions o = options();
        o.replaceExisting = true;
        o.replaced = mode;
        const QList<PlannedMove> plan = linuxbee::planOrganise({ a }, o);
        QVERIFY(at(plan, 0).replaces);
        const OrganiseReport r = linuxbee::executeOrganise(plan, o);
        QCOMPARE(r.done, 1);
        QCOMPARE(bytesOf(target), fixtureBytes("basic.mp3"));
        QVERIFY(!QFileInfo::exists(a));
        // O-CONFLICT: nothing else is left beside the new file.
        QCOMPARE(QDir(QFileInfo(target).absolutePath()).entryList(QDir::Files | QDir::Hidden),
            QStringList({ s(u"01-11 Someone like You.mp3") }));
    }
}

void OrganiseTest::changedSincePlanning()
{
    const QString a = put("basic.mp3", s(u"in/a/x.mp3"));
    const QString b = put("basic.flac", s(u"in/a/y.flac"));
    const OrganiseOptions o = options();
    const QList<PlannedMove> plan = linuxbee::planOrganise({ a, b }, o);
    // Between planning and running: something appears at a's target, and b disappears.
    touch(s(u"out/") + kMp3, "surprise");
    QVERIFY(QFile::remove(b));
    const OrganiseReport r = linuxbee::executeOrganise(plan, o);
    // O-EXECUTE: nothing is overwritten that the plan did not say would be.
    QVERIFY(at(r.outcomes, 0).status == MoveOutcome::Status::Failed);
    QVERIFY(!at(r.outcomes, 0).message.isEmpty());
    QCOMPARE(bytesOf(m_work + s(u"/out/") + kMp3), QByteArray("surprise"));
    QCOMPARE(bytesOf(a), fixtureBytes("basic.mp3"));
    QVERIFY(at(r.outcomes, 1).status == MoveOutcome::Status::Failed);
    QCOMPARE(r.failed, 2);
    QCOMPARE(r.done, 0);
    QVERIFY(QFileInfo::exists(m_work + s(u"/in/a"))); // not empty, not removed
}

void OrganiseTest::otherFiles()
{
    // W-O "also move remaining non-media files".
    const QString a = put("basic.mp3", s(u"in/a/x.mp3"));
    touch(s(u"in/a/cover.jpg"), "jpg");
    touch(s(u"in/a/album.cue"), "cue");
    touch(s(u"in/a/.hidden"), "h");
    touch(s(u"in/a/sub/deeper.txt"), "t"); // O-OTHER: files directly in the folder only
    // A folder whose tracks go to two places keeps its other files.
    const QString b1 = put("basic.mp3", s(u"in/b/x.mp3"));
    const QString b2 = put("basic.flac", s(u"in/b/y.flac"));
    touch(s(u"in/b/cover.jpg"), "b");
    // A folder where a track stays behind keeps them too.
    const QString c1 = put("basic.m4a", s(u"in/c/z.m4a"));
    const QString c2 = put("basic.ogg", s(u"in/c/o.ogg"));
    touch(s(u"in/c/cover.jpg"), "c");

    OrganiseOptions o = options();
    o.moveOtherFiles = true;
    const OrganiseReport r = linuxbee::executeOrganise(linuxbee::planOrganise({ a, b1, b2, c1, c2 }, o), o);
    QCOMPARE(r.failed, 0);
    const QString album = s(u"out/Adele/21/");
    QCOMPARE(tree(),
        QStringList({ s(u"in/a/sub/deeper.txt"), s(u"in/b/cover.jpg"), s(u"in/c/cover.jpg"), s(u"in/c/o.ogg"),
            album + s(u".hidden"), album + s(u"01-11 Someone like You.mp3"),
            album + s(u"01-11 Someone like You_1.mp3"), album + s(u"album.cue"), album + s(u"cover.jpg"),
            s(u"out/") + kM4a, s(u"out/") + kFlac }));
    QCOMPARE(bytesOf(m_work + QLatin1Char('/') + album + s(u"cover.jpg")), QByteArray("jpg"));
    // O-OTHER: reported after the planned moves, in name order, flagged.
    QCOMPARE(r.outcomes.size(), 8);
    QStringList others;
    for (int i = 5; i < 8; ++i) {
        QVERIFY(at(r.outcomes, i).otherFile);
        QVERIFY(at(r.outcomes, i).status == MoveOutcome::Status::Done);
        others.append(QFileInfo(at(r.outcomes, i).source).fileName());
    }
    QCOMPARE(others, QStringList({ s(u".hidden"), s(u"album.cue"), s(u"cover.jpg") }));
    QCOMPARE(r.done, 7);

    // O-OTHER: an other file never replaces one that is already there.
    const QString d = put("basic.wav", s(u"in/d/w.wav"));
    touch(s(u"in/d/cover.jpg"), "newer");
    OrganiseOptions flat = o;
    flat.useTemplate = false;
    flat.targetRoot = m_work + s(u"/out/Adele/21");
    const OrganiseReport r2 = linuxbee::executeOrganise(linuxbee::planOrganise({ d }, flat), flat);
    QCOMPARE(r2.outcomes.size(), 2);
    QVERIFY(at(r2.outcomes, 1).otherFile);
    QVERIFY(at(r2.outcomes, 1).status == MoveOutcome::Status::Skipped);
    QCOMPARE(bytesOf(m_work + QLatin1Char('/') + album + s(u"cover.jpg")), QByteArray("jpg"));
    QCOMPARE(bytesOf(m_work + s(u"/in/d/cover.jpg")), QByteArray("newer"));
}

void OrganiseTest::emptyFolders()
{
    const QString a = put("basic.mp3", s(u"in/a/b/c/x.mp3"));
    OrganiseOptions o = options();
    o.removeEmptyFolders = false;
    OrganiseReport r = linuxbee::executeOrganise(linuxbee::planOrganise({ a }, o), o);
    QCOMPARE(r.done, 1);
    QVERIFY(QFileInfo::exists(m_work + s(u"/in/a/b/c")));
    QVERIFY(r.removedFolders.isEmpty());

    // O-EMPTY: upwards while empty; it stops by itself at the first folder that
    // still holds something (here m_work, which holds out/).
    const QString b = put("basic.flac", s(u"in/a/b/c/y.flac"));
    o.removeEmptyFolders = true;
    r = linuxbee::executeOrganise(linuxbee::planOrganise({ b }, o), o);
    QCOMPARE(r.removedFolders, QStringList({ m_work + s(u"/in/a/b/c"), m_work + s(u"/in/a/b"),
                                   m_work + s(u"/in/a"), m_work + s(u"/in") }));
    QVERIFY(QFileInfo::exists(m_work));
    QVERIFY(!QFileInfo::exists(m_work + s(u"/in")));

    // A rename inside one folder removes nothing.
    const QString d = put("basic.m4a", s(u"solo/z.m4a"));
    OrganiseOptions inPlace = options();
    inPlace.targetRoot.clear();
    r = linuxbee::executeOrganise(linuxbee::planOrganise({ d }, inPlace), inPlace);
    QCOMPARE(r.done, 1);
    QVERIFY(QFileInfo::exists(m_work + s(u"/solo/01-03 M4A Title.m4a")));
    QVERIFY(r.removedFolders.isEmpty());
}

void OrganiseTest::progressAndCancel()
{
    const QStringList files = { put("basic.mp3", s(u"in/a/x.mp3")), put("basic.ogg", s(u"in/a/o.ogg")),
        put("basic.flac", s(u"in/a/y.flac")), put("basic.m4a", s(u"in/a/z.m4a")) };
    const OrganiseOptions o = options();
    QList<QPair<int, int>> calls;
    // O-EXECUTE: asked before each planned move, skipped ones included.
    const OrganiseReport r = linuxbee::executeOrganise(linuxbee::planOrganise(files, o), o,
        [&calls](int index, int count) {
            calls.append({ index, count });
            return index < 2; // stop when asked about the third
        });
    QCOMPARE(calls, (QList<QPair<int, int>> { { 0, 4 }, { 1, 4 }, { 2, 4 } }));
    QVERIFY(r.cancelled);
    QCOMPARE(r.outcomes.size(), 2);
    QCOMPARE(r.done, 1);
    QCOMPARE(r.skipped, 1);
    // What was done stays done; the rest is untouched; in/a is not empty.
    QCOMPARE(tree(), QStringList({ s(u"in/a/o.ogg"), s(u"in/a/y.flac"), s(u"in/a/z.m4a"), s(u"out/") + kMp3 }));
}

void OrganiseTest::failure()
{
    const QString a = put("basic.mp3", s(u"in/a/x.mp3"));
    const QString b = put("basic.flac", s(u"in/a/y.flac"));
    const QString locked = m_work + s(u"/out/Adele");
    QVERIFY(QDir().mkpath(locked));
    QFile::setPermissions(locked, QFile::ReadOwner | QFile::ExeOwner);
    const OrganiseOptions o = options();
    const OrganiseReport r = linuxbee::executeOrganise(linuxbee::planOrganise({ a, b }, o), o);
    QFile::setPermissions(locked, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    // O-EXECUTE: one failure does not stop the run, and the failed file is where it was.
    QVERIFY(at(r.outcomes, 0).status == MoveOutcome::Status::Failed);
    QVERIFY(!at(r.outcomes, 0).message.isEmpty());
    QCOMPARE(bytesOf(a), fixtureBytes("basic.mp3"));
    QVERIFY(at(r.outcomes, 1).status == MoveOutcome::Status::Done);
    QCOMPARE(r.failed, 1);
    QCOMPARE(r.done, 1);
    QVERIFY(r.removedFolders.isEmpty());
}

QTEST_APPLESS_MAIN(OrganiseTest)
#include "tst_organise.moc"
