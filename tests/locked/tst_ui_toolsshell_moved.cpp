// LOCKED ACCEPTANCE TEST (the tools shell: organised files keep their place in the playlists). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/tools-shell.md (TS-ORGANISE), docs/spec/library-shell.md (SH-MOVED)
// and docs/spec/library.md (L-MOVED). Copies of the fixtures in a temporary folder.
#include <QAction>
#include <QLineEdit>
#include <QPointer>
#include <QSettings>
#include <QStatusBar>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "actions/actionregistry.h"
#include "convert/convertdialog.h"
#include "mainwindow/mainwindow.h"
#include "navigator/navigatorpanel.h"
#include "organise/organisedialog.h"
#include "preferences/hotkeyspage.h"
#include "preferences/preferencesdialog.h"
#include "shell/libraryshell.h"
#include "shell/toolsshell.h"
#include "tageditor/tageditordialog.h"
#include "tracklist/tracklistpage.h"

using namespace linuxbee;
using namespace linuxbee::ui;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

QStringList sorted(QStringList list)
{
    list.sort();
    return list;
}

struct Rig {
    QTemporaryDir dir;
    QString a, b, tone;
    std::unique_ptr<Library> library;
    PlaylistStore playlists;
    QSettings settings;
    ActionRegistry actions;
    MainWindow window;
    QDateTime now = QDateTime(QDate(2026, 11, 13), QTime(12, 0), QTimeZone::UTC);
    std::unique_ptr<LibraryShell> lib;
    std::unique_ptr<ToolsShell> tools;
    int presented = 0;
    QString kind; // class name of the dialog last presented
    QPointer<QDialog> last;
    std::function<void(QDialog *)> work; // what "the user" does with the dialog

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
        lib = std::make_unique<LibraryShell>(&window, &actions, library.get(), &playlists, &settings, ShellHooks { });
        tools = std::make_unique<ToolsShell>(&window, &actions, library.get(), lib.get(), &settings, [this](QDialog *dialog) {
            ++presented;
            last = dialog;
            kind = QString::fromLatin1(dialog->metaObject()->className());
            if (work)
                work(dialog);
        });
    }
    QString made(const QString &name)
    {
        QDir(dir.path()).mkpath(name);
        return dir.path() + QLatin1Char('/') + name;
    }
    QString put(const char *from, const char *to)
    {
        const QString target = dir.path() + QLatin1Char('/') + QLatin1String(to);
        if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA) + QLatin1Char('/') + QLatin1String(from), target))
            QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
        return target;
    }
    void run(const char16_t *id)
    {
        if (QAction *action = actions.action(s(id)))
            action->trigger();
        else
            QTest::qFail("no such action", __FILE__, __LINE__);
    }
    void select(const QStringList &paths)
    {
        QList<qint64> ids;
        for (const QString &p : paths)
            if (const auto t = library->trackByPath(p))
                ids.append(t->id);
        lib->trackList()->selectTracks(ids);
    }
    QString title(const QString &path) const
    {
        const auto t = library->trackByPath(path);
        return t ? t->tags.first(QStringLiteral("Title")) : QStringLiteral("NOT IN THE LIBRARY");
    }
};

void type(QDialog *dialog, const char16_t *name, const QString &text)
{
    if (auto *edit = dialog->findChild<QLineEdit *>(s(name))) {
        // Keys, as a person types: a line edit knows it was modified only then.
        edit->setFocus();
        edit->selectAll();
        if (text.isEmpty())
            QTest::keyClick(edit, Qt::Key_Delete);
        else
            QTest::keyClicks(edit, text);
    } else {
        QTest::qFail(qPrintable(QStringLiteral("no line edit named ") + s(name)), __FILE__, __LINE__);
    }
}

}

class ToolsShellMovedTest : public QObject {
    Q_OBJECT
private slots:
    void organisedFilesStayInTheirPlaylists();
};

void ToolsShellMovedTest::organisedFilesStayInTheirPlaylists()
{
    Rig r;
    (void)&sorted;
    const QString out = r.made(s(u"sorted"));
    const QString mix = r.playlists.create(s(u"Mix"));
    const QString quiet = r.playlists.create(s(u"Quiet"));
    QVERIFY(r.playlists.setTracks(mix, { r.a, r.tone, r.b, r.a }));
    QVERIFY(r.playlists.setTracks(quiet, { r.tone }));
    QSignalSpy moved(r.library.get(), &Library::pathsMoved);
    QSignalSpy written(&r.playlists, &PlaylistStore::tracksChanged);
    r.select({ r.a, r.b });
    r.work = [&](QDialog *dialog) {
        auto *organiser = qobject_cast<OrganiseDialog *>(dialog);
        QVERIFY(organiser);
        type(organiser, u"targetEdit", out);
        type(organiser, u"templateEdit", s(u"<Title>"));
        QVERIFY(organiser->run());
    };
    r.run(u"file.organise");
    const QString movedA = out + s(u"/Flac Title.flac");
    const QString movedB = out + s(u"/M4A Title.m4a");
    QVERIFY(QFile::exists(movedA));
    QCOMPARE(r.tools->lastMessage(), s(u"Organised: 2 done, 0 skipped, 0 failed"));
    // SH-MOVED: the playlist names the files where they are now, in the same places.
    QCOMPARE(r.playlists.tracks(mix), QStringList({ movedA, r.tone, movedB, movedA }));
    QCOMPARE(r.playlists.tracks(quiet), QStringList({ r.tone }));
    // Speed (TS-ORGANISE): one run is one pathsMoved, and each playlist that names a moved file is written once.
    QCOMPARE(moved.count(), 1);
    using Moves = QHash<QString, QString>;
    QCOMPARE(moved.at(0).at(0).value<Moves>().size(), 2);
    QCOMPARE(written.count(), 1);
    QCOMPARE(written.at(0).at(0).toString(), mix);
    // The playlist's view shows them: they are tracks of the library at their new paths.
    QVERIFY(r.lib->navigator()->selectNode(s(u"playlist:") + mix));
    QCOMPARE(r.lib->shownPaths().size(), 3); // a, tone, b: a track is shown once
    QVERIFY(r.lib->shownPaths().contains(movedA));
    QVERIFY(r.lib->shownPaths().contains(movedB));
}

QTEST_MAIN(ToolsShellMovedTest)
#include "tst_ui_toolsshell_moved.moc"
