// LOCKED ACCEPTANCE TEST (review of plan 32). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/tools-shell.md (TS-x). Runs on the
// offscreen platform, end to end: the real dialogs, the real tag writer,
// organiser and converter, on copies of the fixtures in a temporary folder.
// Instead of a person, a function is handed the dialog and works it.
#include <QAction>
#include <QCheckBox>
#include <QLineEdit>
#include <QPointer>
#include <QSettings>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QtTest>

#include "actions/actionregistry.h"
#include "convert/convertdialog.h"
#include "mainwindow/mainwindow.h"
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

void tick(QDialog *dialog, const char16_t *name)
{
    auto *box = dialog->findChild<QCheckBox *>(s(name));
    QVERIFY(box);
    if (!box->isChecked())
        QTest::mouseClick(box, Qt::LeftButton);
    QVERIFY(box->isChecked());
}

class ToolsShellReviewTest : public QObject {
    Q_OBJECT
private slots:
    void organiseReplacingALibraryTrack();
    void organiseOntoATrackWhoseFileIsGone();
};

void ToolsShellReviewTest::organiseReplacingALibraryTrack()
{
    // TS-ORGANISE with O-REPLACED: the moved track replaces a file the library
    // also holds. The moved track keeps its id and statistics at the target;
    // the track of the replaced file goes, since its file is no longer there.
    Rig r;
    const QString out = r.made(s(u"sorted"));
    const QString target = out + s(u"/Flac Title.flac");
    QVERIFY(QFile::copy(r.a, target));
    QVERIFY(r.library->addFile(target, s(u"Music"), r.now) != 0);
    const qint64 idA = r.library->trackByPath(r.a)->id;
    const qint64 idOld = r.library->trackByPath(target)->id;
    QVERIFY(r.library->recordPlay(idA, r.now));
    QVERIFY(r.library->recordPlay(idA, r.now));
    r.select({ r.a });
    r.work = [&](QDialog *dialog) {
        type(dialog, u"targetEdit", out);
        type(dialog, u"templateEdit", s(u"<Title>"));
        tick(dialog, u"replaceCheck");
        QVERIFY(qobject_cast<OrganiseDialog *>(dialog)->run());
    };
    r.run(u"file.organise");
    QCOMPARE(r.tools->lastMessage(), s(u"Organised: 1 done, 0 skipped, 0 failed"));
    QVERIFY(!QFile::exists(r.a));
    QVERIFY(QFile::exists(target));
    QVERIFY(!r.library->trackByPath(r.a).has_value()); // nothing points at the old place
    const auto moved = r.library->trackByPath(target);
    QVERIFY(moved.has_value());
    QCOMPARE(moved->id, idA);
    QCOMPARE(moved->playCount, 2);
    QVERIFY(!moved->missing);
    QVERIFY(!r.library->track(idOld).has_value());
    QCOMPARE(r.library->count(), 3);
}

void ToolsShellReviewTest::organiseOntoATrackWhoseFileIsGone()
{
    // TS-ORGANISE: the library still holds a track at the target, but its file
    // is gone, so nothing is replaced on disk. The rule is the same: the moved
    // track takes the path, and the track that was there goes.
    Rig r;
    const QString out = r.made(s(u"sorted"));
    const QString target = out + s(u"/Flac Title.flac");
    QVERIFY(QFile::copy(r.a, target));
    QVERIFY(r.library->addFile(target, s(u"Music"), r.now) != 0);
    const qint64 idA = r.library->trackByPath(r.a)->id;
    const qint64 idOld = r.library->trackByPath(target)->id;
    QVERIFY(r.library->recordPlay(idA, r.now));
    QVERIFY(QFile::remove(target)); // the file went behind the library's back
    r.select({ r.a });
    r.work = [&](QDialog *dialog) {
        type(dialog, u"targetEdit", out);
        type(dialog, u"templateEdit", s(u"<Title>"));
        QVERIFY(qobject_cast<OrganiseDialog *>(dialog)->run()); // "replace" is not ticked: there is nothing to replace
    };
    r.run(u"file.organise");
    QCOMPARE(r.tools->lastMessage(), s(u"Organised: 1 done, 0 skipped, 0 failed"));
    QVERIFY(!QFile::exists(r.a));
    QVERIFY(QFile::exists(target));
    QVERIFY(!r.library->trackByPath(r.a).has_value());
    const auto moved = r.library->trackByPath(target);
    QVERIFY(moved.has_value());
    QCOMPARE(moved->id, idA);
    QCOMPARE(moved->playCount, 1);
    QVERIFY(!r.library->track(idOld).has_value());
    QCOMPARE(r.library->count(), 3);
}

QTEST_MAIN(ToolsShellReviewTest)
#include "tst_ui_toolsshell_review.moc"
