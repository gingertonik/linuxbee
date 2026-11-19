// LOCKED ACCEPTANCE TEST (the window says which playlists did not follow files that moved).
// Written by the reviewer, not the builder. The builder makes these pass by changing src/, never
// by editing this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values: SH-MOVED in docs/spec/library-shell.md, Y-RELINK in docs/spec/playlists.md.
// Offscreen, with the real window, library and store, in a temporary folder. A playlist that
// cannot be read is one whose file gives its owner no leave to read it.
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include <unistd.h>

#include "actions/actionregistry.h"
#include "library/library.h"
#include "mainwindow/mainwindow.h"
#include "playlists/playliststore.h"
#include "shell/libraryshell.h"

using namespace linuxbee;
using namespace linuxbee::ui;

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

struct Rig {
    QTemporaryDir dir;
    QString a, b;
    std::unique_ptr<Library> library;
    PlaylistStore playlists;
    QSettings settings;
    ActionRegistry actions;
    MainWindow window;
    QDateTime now = QDateTime(QDate(2026, 11, 19), QTime(12, 0), QTimeZone::UTC);
    std::unique_ptr<LibraryShell> shell;
    QStringList locked;

    Rig()
        : library(Library::open(QStringLiteral(":memory:")))
        , playlists(made(QStringLiteral("playlists")))
        , settings(dir.path() + QStringLiteral("/s.ini"), QSettings::IniFormat)
        , window(&actions, &settings)
    {
        made(QStringLiteral("music"));
        a = put("basic.flac", "music/a.flac");
        b = put("basic.mp3", "music/b.mp3");
        for (const QString &p : { a, b })
            if (library->addFile(p, QStringLiteral("Music"), now) == 0)
                QTest::qFail("a fixture did not go into the library", __FILE__, __LINE__);
        ShellHooks hooks;
        hooks.now = [this] { return now; };
        shell = std::make_unique<LibraryShell>(
            &window, &actions, library.get(), &playlists, &settings, hooks);
    }
    ~Rig()
    {
        for (const QString &path : locked)
            QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
    }
    QString made(const QString &name)
    {
        QDir(dir.path()).mkpath(name);
        return dir.path() + QLatin1Char('/') + name;
    }
    QString put(const char *from, const char *to)
    {
        const QString target = dir.path() + QLatin1Char('/') + QLatin1String(to);
        if (!QFile::copy(
                QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/") + QLatin1String(from),
                target))
            QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
        return target;
    }
    // A playlist that names a.flac. With `readable` false its file cannot be read.
    QString playlist(const char *name, bool readable)
    {
        const QString path = dir.path() + QStringLiteral("/playlists/") + QLatin1String(name)
            + QStringLiteral(".m3u8");
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(a.toUtf8() + "\n") < 0)
            QTest::qFail("could not make a playlist", __FILE__, __LINE__);
        file.close();
        if (!readable) {
            QFile::setPermissions(path, QFile::Permissions());
            locked.append(path);
        }
        return path;
    }
    // The music folder is somewhere else now: the library says every path moved.
    int theFolderMoves()
    {
        return library->relinkPaths(
            dir.path() + QStringLiteral("/music"), dir.path() + QStringLiteral("/elsewhere"));
    }
    QByteArray moved() const
    {
        return (dir.path() + QStringLiteral("/elsewhere/a.flac\n")).toUtf8();
    }
};

}

class LibraryShellRelinkLeftTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void everyPlaylistFollows();
    void oneDidNot();
    void twoDidNot();
    void moreThanThree();
};

void LibraryShellRelinkLeftTest::initTestCase()
{
    if (::geteuid() == 0)
        QSKIP("the owner of everything can read anything");
}

void LibraryShellRelinkLeftTest::everyPlaylistFollows()
{
    // The control: nothing is said when nothing was left.
    Rig r;
    const QString good = r.playlist("Good", true);
    const QString before = r.shell->lastMessage();
    QCOMPARE(r.theFolderMoves(), 2);
    QCOMPARE(bytesOf(good), r.moved());
    QCOMPARE(r.shell->lastMessage(), before);
}

void LibraryShellRelinkLeftTest::oneDidNot()
{
    Rig r;
    const QString good = r.playlist("Good", true);
    r.playlist("Shut", false);
    QCOMPARE(r.theFolderMoves(), 2);
    QCOMPARE(bytesOf(good), r.moved());
    QCOMPARE(r.shell->lastMessage(),
        s(u"1 playlist was not updated for the files that moved (it could not be read or written): "
          u"Shut"));
}

void LibraryShellRelinkLeftTest::twoDidNot()
{
    Rig r;
    r.playlist("Good", true);
    r.playlist("Shut", false);
    r.playlist("Also shut", false);
    QCOMPARE(r.theFolderMoves(), 2);
    QCOMPARE(r.shell->lastMessage(),
        s(u"2 playlists were not updated for the files that moved (they could not be read or "
          u"written): "
          u"Also shut, Shut"));
}

void LibraryShellRelinkLeftTest::moreThanThree()
{
    // Three are named, in the store's order; the rest are counted.
    Rig r;
    for (const char *name : { "E", "A", "D", "B", "C" })
        r.playlist(name, false);
    QCOMPARE(r.theFolderMoves(), 2);
    QCOMPARE(r.shell->lastMessage(),
        s(u"5 playlists were not updated for the files that moved (they could not be read or "
          u"written): "
          u"A, B, C and 2 more"));
}

QTEST_MAIN(LibraryShellRelinkLeftTest)
#include "tst_ui_libraryshell_relink_left.moc"
