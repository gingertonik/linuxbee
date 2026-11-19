// LOCKED ACCEPTANCE TEST (the tools shell: a run of Organise has one line, and what the playlists
// did not follow is in it). Written by the reviewer, not the builder. The builder makes these pass
// by changing src/, never by editing this file. A test believed to be wrong is reported, with the
// reason, and left alone.
//
// Expected values: TS-ORGANISE in docs/spec/tools-shell.md, SH-MOVED in docs/spec/library-shell.md.
// Offscreen, with the real window and both shells, made in the order the program makes them. What
// is compared is what the status bar shows when the command has returned, not only what a shell
// remembers having said. A playlist that cannot be read is one whose file gives its owner no leave
// to read it.
#include <QAction>
#include <QFile>
#include <QLineEdit>
#include <QSettings>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QtTest>

#include <unistd.h>

#include "actions/actionregistry.h"
#include "library/library.h"
#include "mainwindow/mainwindow.h"
#include "organise/organisedialog.h"
#include "playlists/playliststore.h"
#include "shell/libraryshell.h"
#include "shell/toolsshell.h"
#include "tracklist/tracklistpage.h"

using namespace linuxbee;
using namespace linuxbee::ui;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

const char16_t *const kPlain = u"Organised: 1 done, 0 skipped, 0 failed";
const char16_t *const kOneLeft = u"1 playlist was not updated for the files that moved (it could "
                                 u"not be read or written): Shut";

struct Rig {
    QTemporaryDir dir;
    QString a, b, tone;
    std::unique_ptr<Library> library;
    PlaylistStore playlists;
    QSettings settings;
    ActionRegistry actions;
    MainWindow window;
    QDateTime now = QDateTime(QDate(2026, 11, 19), QTime(12, 0), QTimeZone::UTC);
    std::unique_ptr<LibraryShell> lib;
    std::unique_ptr<ToolsShell> tools;
    std::function<void(QDialog *)> work; // what "the user" does with the dialog
    QStringList locked;

    Rig()
        : library(Library::open(QStringLiteral(":memory:")))
        , playlists(made(QStringLiteral("playlists")))
        , settings(dir.path() + QStringLiteral("/s.ini"), QSettings::IniFormat)
        , window(&actions, &settings)
    {
        made(QStringLiteral("music"));
        a = put("tags/basic.flac", "music/a.flac");
        b = put("tags/basic.m4a", "music/b.m4a");
        tone = put("convert/tone.flac", "music/tone.flac");
        for (const QString &p : { a, b, tone })
            if (library->addFile(p, QStringLiteral("Music"), now) == 0)
                QTest::qFail("a fixture did not go into the library", __FILE__, __LINE__);
        lib = std::make_unique<LibraryShell>(
            &window, &actions, library.get(), &playlists, &settings, ShellHooks { });
        tools = std::make_unique<ToolsShell>(
            &window, &actions, library.get(), lib.get(), &settings, [this](QDialog *dialog) {
                if (work)
                    work(dialog);
            });
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
                QStringLiteral(LINUXBEE_TEST_DATA) + QLatin1Char('/') + QLatin1String(from),
                target))
            QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
        return target;
    }
    // A playlist that names a.flac, and that cannot be read.
    QString shut(const char *name)
    {
        const QString path = dir.path() + QStringLiteral("/playlists/") + QLatin1String(name)
            + QStringLiteral(".m3u8");
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(a.toUtf8() + "\n") < 0)
            QTest::qFail("could not make a playlist", __FILE__, __LINE__);
        file.close();
        QFile::setPermissions(path, QFile::Permissions());
        locked.append(path);
        return path;
    }
    void select(const QString &path)
    {
        if (const auto t = library->trackByPath(path))
            lib->trackList()->selectTracks({ t->id });
        else
            QTest::qFail("the track to select is not in the library", __FILE__, __LINE__);
    }
    void organise()
    {
        if (QAction *action = actions.action(QStringLiteral("file.organise")))
            action->trigger();
        else
            QTest::qFail("no such action", __FILE__, __LINE__);
    }
    // The file at `from` goes to `to` on the disk, and the organiser reports that one outcome to
    // the shell, as its dialog does. With `moved` false the report calls it a copy.
    void tell(const QString &from, const QString &to, bool moved = true)
    {
        if (!QFile::rename(from, to))
            QTest::qFail("could not move a file", __FILE__, __LINE__);
        select(tone);
        work = [&](QDialog *dialog) {
            auto *organiser = qobject_cast<OrganiseDialog *>(dialog);
            if (!organiser) {
                QTest::qFail("the dialog is not the organiser", __FILE__, __LINE__);
                return;
            }
            OrganiseReport report;
            MoveOutcome outcome;
            outcome.status = MoveOutcome::Status::Done;
            outcome.source = from;
            outcome.target = to;
            outcome.moved = moved;
            report.outcomes.append(outcome);
            report.done = 1;
            emit organiser->organised(report);
        };
        organise();
    }
    QString shown() const { return window.statusBar()->currentMessage(); }
};

void type(QDialog *dialog, const char16_t *name, const QString &text)
{
    if (auto *edit = dialog->findChild<QLineEdit *>(s(name))) {
        // Keys, as a person types: a line edit knows it was modified only then.
        edit->setFocus();
        edit->selectAll();
        QTest::keyClicks(edit, text);
    } else {
        QTest::qFail(
            qPrintable(QStringLiteral("no line edit named ") + s(name)), __FILE__, __LINE__);
    }
}

}

class ToolsShellOrganiseLineTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void theRunHasOneLine();
    void nothingLeftNothingAdded();
    void whatAnEarlierRunLeftIsNotSaidAgain();
    void aRunThatMovedNothingSaysNothingOfPlaylists();
    void fourAreThreeNamedAndOneMore();
};

void ToolsShellOrganiseLineTest::initTestCase()
{
    if (::geteuid() == 0)
        QSKIP("the owner of everything can read anything");
}

void ToolsShellOrganiseLineTest::theRunHasOneLine()
{
    // The whole command, with the real dialog: the line that stays on show names the run and the
    // playlist that did not follow.
    Rig r;
    const QString out = r.made(s(u"sorted"));
    const QString good = r.playlists.create(s(u"Good"));
    QVERIFY(r.playlists.setTracks(good, { r.a, r.tone }));
    r.shut("Shut");
    r.window.show();
    r.select(r.a);
    r.work = [&](QDialog *dialog) {
        auto *organiser = qobject_cast<OrganiseDialog *>(dialog);
        QVERIFY(organiser);
        type(organiser, u"targetEdit", out);
        type(organiser, u"templateEdit", s(u"<Title>"));
        QVERIFY(organiser->run());
    };
    r.organise();
    const QString movedA = out + s(u"/Flac Title.flac");
    QVERIFY(QFile::exists(movedA));
    QCOMPARE(r.playlists.tracks(good), QStringList({ movedA, r.tone }));
    const QString line = s(kPlain) + s(u"; ") + s(kOneLeft);
    QCOMPARE(r.shown(), line);
    QCOMPARE(r.tools->lastMessage(), line);
    // SH-MOVED is as it was: the library shell said its own sentence.
    QCOMPARE(r.lib->lastMessage(), s(kOneLeft));
    // And it stays: nothing waits to write over it.
    QTest::qWait(120);
    QCOMPARE(r.shown(), line);
}

void ToolsShellOrganiseLineTest::nothingLeftNothingAdded()
{
    // The control: every playlist followed, and the line is the one it always was.
    Rig r;
    const QString good = r.playlists.create(s(u"Good"));
    QVERIFY(r.playlists.setTracks(good, { r.a, r.b }));
    const QString n = r.made(s(u"sorted")) + s(u"/n.m4a");
    r.tell(r.b, n);
    QCOMPARE(r.playlists.tracks(good), QStringList({ r.a, n }));
    QCOMPARE(r.shown(), s(kPlain));
    QCOMPARE(r.tools->lastMessage(), s(kPlain));
}

void ToolsShellOrganiseLineTest::whatAnEarlierRunLeftIsNotSaidAgain()
{
    // The second run left nothing: its line says nothing of playlists, though the library shell
    // still remembers the sentence of the first.
    Rig r;
    const QString shut = r.shut("Shut");
    const QString out = r.made(s(u"sorted"));
    r.tell(r.b, out + s(u"/n.m4a"));
    QCOMPARE(r.shown(), s(kPlain) + s(u"; ") + s(kOneLeft));
    QVERIFY(QFile::setPermissions(shut, QFile::ReadOwner | QFile::WriteOwner));
    r.tell(r.a, out + s(u"/n.flac"));
    QCOMPARE(r.lib->lastMessage(), s(kOneLeft));
    QCOMPARE(r.shown(), s(kPlain));
    QCOMPARE(r.tools->lastMessage(), s(kPlain));
    QCOMPARE(r.playlists.tracks(s(u"Shut")), QStringList({ out + s(u"/n.flac") }));
}

void ToolsShellOrganiseLineTest::aRunThatMovedNothingSaysNothingOfPlaylists()
{
    // A run of copies records no move, so no playlist was asked to follow.
    Rig r;
    r.shut("Shut");
    const QString out = r.made(s(u"sorted"));
    r.tell(r.b, out + s(u"/n.m4a"));
    QCOMPARE(r.shown(), s(kPlain) + s(u"; ") + s(kOneLeft));
    r.tell(r.a, out + s(u"/copy.flac"), false);
    QCOMPARE(r.shown(), s(kPlain));
    QCOMPARE(r.tools->lastMessage(), s(kPlain));
}

void ToolsShellOrganiseLineTest::fourAreThreeNamedAndOneMore()
{
    // SH-MOVED: of more than three, three are named. Four is the first such number.
    Rig r;
    for (const char *name : { "D", "B", "A", "C" })
        r.shut(name);
    r.tell(r.b, r.made(s(u"sorted")) + s(u"/n.m4a"));
    const QString left = s(u"4 playlists were not updated for the files that moved (they could not "
                           u"be read or written): A, B, C and 1 more");
    QCOMPARE(r.lib->lastMessage(), left);
    QCOMPARE(r.shown(), s(kPlain) + s(u"; ") + left);
}

QTEST_MAIN(ToolsShellOrganiseLineTest)
#include "tst_ui_toolsshell_organise_line.moc"
