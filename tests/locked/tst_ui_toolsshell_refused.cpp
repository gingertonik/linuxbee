// LOCKED ACCEPTANCE TEST (the tools shell: Organise says so when the library could not record the moves). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/tools-shell.md (TS-ORGANISE) and docs/spec/library.md (L-MOVED, L-FAILED).
// The library is a file here; a trigger planted through a second connection refuses one step of the recording.
#include <QAction>
#include <QLineEdit>
#include <QPointer>
#include <QSettings>
#include <QStatusBar>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
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
        : library(Library::open(dir.path() + QStringLiteral("/library.db")))
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

void other(const Rig &r, const QString &statement)
{
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("other"));
        db.setDatabaseName(r.dir.path() + QStringLiteral("/library.db"));
        QSqlQuery query(db);
        if (!db.open() || !query.exec(statement))
            QTest::qFail(qPrintable(query.lastError().text()), __FILE__, __LINE__);
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("other"));
}

// The organiser's report of moves that happened, given to the shell as the dialog gives it.
void tell(Rig &r, const QList<QPair<QString, QString>> &moves)
{
    r.select({ r.b });
    r.work = [&](QDialog *dialog) {
        auto *organiser = qobject_cast<OrganiseDialog *>(dialog);
        QVERIFY(organiser);
        OrganiseReport told;
        for (const auto &move : moves) {
            MoveOutcome outcome;
            outcome.status = MoveOutcome::Status::Done;
            outcome.source = move.first;
            outcome.target = move.second;
            outcome.moved = true;
            told.outcomes.append(outcome);
        }
        told.done = int(moves.size());
        emit organiser->organised(told);
    };
    r.run(u"file.organise");
}

}

class ToolsShellRefusedTest : public QObject {
    Q_OBJECT
private slots:
    void organiseSaysTheLibraryIsBehind();
};

void ToolsShellRefusedTest::organiseSaysTheLibraryIsBehind()
{
    // TS-ORGANISE: tone takes a's place (replacing it), then the file at a goes on to n; the library refuses
    // to let a's track go. The files are where Organise put them, the library is as it was, and the line says so.
    Rig r;
    (void)&sorted;
    (void)&type;
    const QString n = r.made(s(u"sorted")) + s(u"/n.flac");
    const QString mix = r.playlists.create(s(u"Mix"));
    QVERIFY(r.playlists.setTracks(mix, { r.tone, r.a, r.b }));
    const qint64 idA = r.library->trackByPath(r.a)->id;
    const qint64 idTone = r.library->trackByPath(r.tone)->id;
    QVERIFY(QFile::remove(r.a));
    QVERIFY(QFile::rename(r.tone, r.a));
    QVERIFY(QFile::rename(r.a, n));
    other(r, QStringLiteral("CREATE TRIGGER refuse_one BEFORE DELETE ON tracks WHEN old.id = %1 BEGIN SELECT RAISE(ABORT, 'refused by the test'); END").arg(idA));
    QSignalSpy moved(r.library.get(), &Library::pathsMoved);
    tell(r, { { r.tone, r.a }, { r.a, n } });
    QCOMPARE(r.tools->lastMessage(),
        s(u"Organised: 2 done, 0 skipped, 0 failed; the library could not be changed and still lists the files where they were"));
    QCOMPARE(r.library->count(), 3);
    QCOMPARE(r.library->track(idA)->path, r.a);
    QCOMPARE(r.library->track(idTone)->path, r.tone);
    QCOMPARE(r.playlists.tracks(mix), QStringList({ r.tone, r.a, r.b }));
    QCOMPARE(moved.count(), 0);
    // With the refusal gone, a run that records is said as usual, and a refusal earlier is not said again.
    other(r, QStringLiteral("DROP TRIGGER refuse_one"));
    tell(r, { { r.tone, r.a }, { r.a, n } });
    QCOMPARE(r.tools->lastMessage(), s(u"Organised: 2 done, 0 skipped, 0 failed"));
    QCOMPARE(r.library->track(idTone)->path, n);
    QCOMPARE(r.playlists.tracks(mix), QStringList({ n, r.a, r.b }));
}

QTEST_MAIN(ToolsShellRefusedTest)
#include "tst_ui_toolsshell_refused.moc"
