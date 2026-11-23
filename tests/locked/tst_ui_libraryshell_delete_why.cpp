// LOCKED ACCEPTANCE TEST (Delete Files says why a file could not go, fix-c1). Written by the
// reviewer, not the builder. The builder makes these pass by changing src/, never by editing this
// file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/library-shell.md (SH-DELETE step 4) and
// docs/spec/delete-files.md (FD-NAMES). Trash F5 of the audit, in the window: a name the trash
// cannot take said only "1 could not be moved". Runs on the offscreen platform with the real
// window, library and shell, on copies of fixtures in a temporary folder, with a trash of this
// test's own (XDG_DATA_HOME).
#include <QAction>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QtTest>

#include "actions/actionregistry.h"
#include "library/library.h"
#include "mainwindow/mainwindow.h"
#include "playlists/playliststore.h"
#include "shell/libraryshell.h"
#include "tracklist/tracklistmodel.h"
#include "tracklist/tracklistpage.h"

using namespace linuxbee;
using namespace linuxbee::ui;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

struct Rig {
    QTemporaryDir dir;
    QString a, b, c, d;
    std::unique_ptr<Library> library;
    PlaylistStore playlists;
    QSettings settings;
    ActionRegistry actions;
    MainWindow window;
    QDateTime now = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);
    std::unique_ptr<LibraryShell> shell;
    bool yes = true;
    QStringList asked;
    int existedWhenAsked = 0; // how many of the four files were there at the question

    Rig()
        : library(Library::open(QStringLiteral(":memory:")))
        , playlists(made(QStringLiteral("playlists")))
        , settings(dir.path() + QStringLiteral("/s.ini"), QSettings::IniFormat)
        , window(&actions, &settings)
    {
        qputenv("XDG_DATA_HOME", QFile::encodeName(dir.path() + QStringLiteral("/xdg-data")));
        made(QStringLiteral("music/album"));
        a = put("basic.flac", "music/album/a.flac");
        b = put("basic.mp3", "music/album/b.mp3");
        c = put("basic.ogg", "music/album/c.ogg");
        d = put("basic.m4a", "music/d.m4a");
        for (const QString &p : { a, b, c, d })
            if (library->addFile(p, QStringLiteral("Music"), now) == 0)
                QTest::qFail("a fixture did not go into the library", __FILE__, __LINE__);
        ShellHooks hooks;
        hooks.now = [this] { return now; };
        hooks.confirm = [this](const QString &question) {
            asked.append(question);
            existedWhenAsked = 0;
            for (const QString &p : { a, b, c, d })
                existedWhenAsked += QFileInfo::exists(p) ? 1 : 0;
            return yes;
        };
        shell = std::make_unique<LibraryShell>(
            &window, &actions, library.get(), &playlists, &settings, hooks);
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
    QString trashed(const char *name) const
    {
        return dir.path() + QStringLiteral("/xdg-data/Trash/files/") + QLatin1String(name);
    }
    qint64 id(const QString &path) const
    {
        const auto t = library->trackByPath(path);
        return t ? t->id : 0;
    }
    void select(const QStringList &paths)
    {
        QList<qint64> ids;
        for (const QString &p : paths)
            ids.append(id(p));
        shell->trackList()->selectTracks(ids);
    }
    void run()
    {
        if (QAction *action = actions.action(QStringLiteral("file.delete")))
            action->trigger();
        else
            QTest::qFail("no such action", __FILE__, __LINE__);
    }
};

#define NEED_DELETE(r)                                                                             \
    do {                                                                                           \
        if (!(r).shell->trackList() || !(r).actions.isImplemented(QStringLiteral("file.delete")))  \
            QFAIL("Delete Files has not been built");                                              \
    } while (false)

}

class LibraryShellDeleteWhyTest : public QObject {
    Q_OBJECT
private slots:
    void oneThatCouldNotGoSaysWhy();
    void twoThatCouldNotGoAreCounted();
};

namespace {

QString longName(int bytes)
{
    return QString(bytes - 5, QLatin1Char('n')) + QStringLiteral(".flac");
}

QString putLong(Rig &r, int bytes)
{
    const QString target = r.dir.path() + QStringLiteral("/music/album/") + longName(bytes);
    if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/basic.flac"), target)
        || r.library->addFile(target, QStringLiteral("Music"), r.now) == 0)
        return QString();
    return target;
}

}

void LibraryShellDeleteWhyTest::oneThatCouldNotGoSaysWhy()
{
    // SH-DELETE step 4: one file that could not go is said with its reason. The other goes.
    Rig r;
    NEED_DELETE(r);
    const QString longOne = putLong(r, 246);
    QVERIFY2(!longOne.isEmpty(), "the long file did not go into the library");
    r.select({ longOne, r.a });
    r.run();
    QVERIFY(QFileInfo::exists(longOne));
    QVERIFY(r.library->trackByPath(longOne).has_value());
    QVERIFY(!QFileInfo::exists(r.a));
    QCOMPARE(r.shell->lastMessage(),
        s(u"Moved 1 file to the trash; 1 could not be moved: the name is too long for the trash: "
          u"246 bytes, and the trash takes names of up to 245"));
}

void LibraryShellDeleteWhyTest::twoThatCouldNotGoAreCounted()
{
    // The other side: two that could not go are counted, as before, with no reason.
    Rig r;
    NEED_DELETE(r);
    const QString one = putLong(r, 246);
    const QString two = putLong(r, 250);
    QVERIFY2(!one.isEmpty() && !two.isEmpty(), "the long files did not go into the library");
    r.select({ one, two });
    r.run();
    QVERIFY(QFileInfo::exists(one) && QFileInfo::exists(two));
    QCOMPARE(r.shell->lastMessage(), s(u"No file was moved to the trash; 2 could not be moved"));
}

QTEST_MAIN(LibraryShellDeleteWhyTest)
#include "tst_ui_libraryshell_delete_why.moc"
