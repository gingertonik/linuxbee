// LOCKED ACCEPTANCE TEST (the accent box on the Library page, and what it reaches). Written by
// the reviewer, not the builder. The builder makes these pass by changing src/, never by editing
// this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/library-preferences.md (LP-STRUCTURE, LP-LOAD,
// LP-APPLY, LP-INSTALL) and docs/spec/library-shell.md (SH-SEARCH). Runs on the offscreen
// platform. No message bus is needed: the background shell is given a connection that does not
// exist.
#include <QCheckBox>
#include <QDBusConnection>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include "actions/actionregistry.h"
#include "library/library.h"
#include "mainwindow/mainwindow.h"
#include "preferences/librarypage.h"
#include "preferences/preferencesdialog.h"
#include "shell/backgroundshell.h"
#include "shell/libraryshell.h"
#include "shell/librarypreferences.h"
#include "shell/playbackshell.h"
#include "shell/toolsshell.h"
#include "tracklist/tracklistmodel.h"

using namespace linuxbee;
using namespace linuxbee::ui;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

const QString kKey = QStringLiteral("library/searchIgnoresAccents");

// A settings file as the next run of the program reads it: every value is text.
QString nextRun(QSettings &settings)
{
    static int count = 0;
    settings.sync();
    const QString copy = settings.fileName() + QStringLiteral(".run%1.ini").arg(++count);
    QFile::remove(copy);
    QFile::copy(settings.fileName(), copy);
    return copy;
}

QCheckBox *box(QWidget &page)
{
    auto *found = page.findChild<QCheckBox *>(s(u"ignoreAccentsBox"));
    if (!found)
        QTest::qFail("no widget named ignoreAccentsBox", __FILE__, __LINE__);
    return found;
}

class Tape : public AudioBackend {
    Q_OBJECT
public:
    void load(const QString &) override { }
    void play() override { }
    void pause() override { }
    void stop() override { }
    void seek(qint64) override { }
    void setVolume(double) override { }
};

}

class LibraryPageAccentsTest : public QObject {
    Q_OBJECT
private slots:
    void structure();
    void loads_data();
    void loads();
    void appliesAndForgets();
    void theShellReadsIt();
    void installed();
};

void LibraryPageAccentsTest::structure()
{
    // LP-STRUCTURE: the box, with words of linuxBee's own; checked when nothing is stored.
    QTemporaryDir dir;
    QSettings settings(dir.path() + s(u"/s.ini"), QSettings::IniFormat);
    LibraryPage page(&settings);
    QCheckBox *b = box(page);
    QVERIFY(b);
    QCOMPARE(b->text(), s(u"Ignore accents when searching"));
    QVERIFY(b->isChecked());
    QVERIFY(!page.isModified());
}

void LibraryPageAccentsTest::loads_data()
{
    QTest::addColumn<QString>("stored");
    QTest::addColumn<bool>("checked");
    QTest::newRow("false") << s(u"false") << false;
    QTest::newRow("FALSE") << s(u"FALSE") << false;
    QTest::newRow("0") << s(u"0") << false;
    QTest::newRow("true") << s(u"true") << true;
    QTest::newRow("1") << s(u"1") << true;
    QTest::newRow("perhaps") << s(u"perhaps") << true;
    QTest::newRow("empty") << QString() << true;
}

void LibraryPageAccentsTest::loads()
{
    // LP-LOAD: checked unless the stored text is false (in any case of letters) or 0.
    QFETCH(QString, stored);
    QFETCH(bool, checked);
    QTemporaryDir dir;
    QSettings settings(dir.path() + s(u"/s.ini"), QSettings::IniFormat);
    settings.setValue(kKey, stored);
    QSettings later(nextRun(settings), QSettings::IniFormat);
    LibraryPage page(&later);
    QCOMPARE(box(page)->isChecked(), checked);
}

void LibraryPageAccentsTest::appliesAndForgets()
{
    // LP-APPLY: the box is part of what is modified and what is stored; load() forgets an edit.
    QTemporaryDir dir;
    QSettings settings(dir.path() + s(u"/s.ini"), QSettings::IniFormat);
    int stored = 0;
    LibraryPage::Hooks hooks;
    hooks.stored = [&] { ++stored; };
    LibraryPage page(&settings, hooks);
    box(page)->setChecked(false);
    QVERIFY(page.isModified());
    QCOMPARE(settings.allKeys(), QStringList()); // nothing stored by editing
    page.load();
    QVERIFY(box(page)->isChecked());
    QVERIFY(!page.isModified());
    box(page)->setChecked(false);
    QCOMPARE(page.apply(), QString());
    QCOMPARE(stored, 1);
    QVERIFY(!page.isModified());
    QSettings later(nextRun(settings), QSettings::IniFormat);
    QCOMPARE(later.value(kKey).toString(), s(u"false"));
    box(page)->setChecked(true);
    page.apply();
    QCOMPARE(stored, 2);
    QSettings again(nextRun(settings), QSettings::IniFormat);
    QCOMPARE(again.value(kKey).toString(), s(u"true"));
    QVERIFY(again.contains(s(u"library/checkOnStartup"))); // the page's other keys, as ever
}

void LibraryPageAccentsTest::theShellReadsIt()
{
    // SH-SEARCH: the library shell gives its track list what the settings hold, when it is made
    // and when it is told to read them again.
    QTemporaryDir dir;
    QDir(dir.path()).mkpath(s(u"playlists"));
    QSettings settings(dir.path() + s(u"/s.ini"), QSettings::IniFormat);
    settings.setValue(kKey, s(u"false"));
    auto library = Library::open(s(u":memory:"));
    PlaylistStore playlists(dir.path() + s(u"/playlists"));
    ActionRegistry actions;
    MainWindow window(&actions, &settings);
    LibraryShell lib(&window, &actions, library.get(), &playlists, &settings, ShellHooks { });
    QVERIFY(!lib.model()->searchIgnoresAccents());
    settings.setValue(kKey, s(u"TRUE"));
    QVERIFY(!lib.model()->searchIgnoresAccents()); // not until it is told
    lib.readSearchSettings();
    QVERIFY(lib.model()->searchIgnoresAccents());
    settings.remove(kKey);
    lib.readSearchSettings();
    QVERIFY(lib.model()->searchIgnoresAccents()); // missing: ignored
    settings.setValue(kKey, s(u"0"));
    lib.readSearchSettings();
    QVERIFY(!lib.model()->searchIgnoresAccents());
}

void LibraryPageAccentsTest::installed()
{
    // LP-INSTALL: applying the page tells the background shell, then calls the hook given last.
    QTemporaryDir dir;
    QDir(dir.path()).mkpath(s(u"playlists"));
    QSettings settings(dir.path() + s(u"/s.ini"), QSettings::IniFormat);
    auto library = Library::open(s(u":memory:"));
    PlaylistStore playlists(dir.path() + s(u"/playlists"));
    ActionRegistry actions;
    MainWindow window(&actions, &settings);
    Tape tape;
    LibraryShell lib(&window, &actions, library.get(), &playlists, &settings, ShellHooks { });
    PlaybackShell play(&window, &actions, library.get(), &lib, &tape, &settings,
        [] { return QDateTime::currentDateTimeUtc(); });
    std::function<void(PreferencesDialog *)> work;
    ToolsShell tools(&window, &actions, library.get(), &lib, &settings, [&](QDialog *dialog) {
        if (auto *prefs = qobject_cast<PreferencesDialog *>(dialog); prefs && work)
            work(prefs);
    });
    BackgroundShell background(
        &window, library.get(), &play, &settings, { }, QDBusConnection(s(u"no-such-connection")));
    int told = 0;
    installLibraryPreferences(&tools, &background, &settings, { }, [&] {
        ++told;
        lib.readSearchSettings();
    });
    QVERIFY(lib.model()->searchIgnoresAccents());
    bool worked = false;
    work = [&](PreferencesDialog *prefs) {
        QVERIFY(prefs->selectPage(s(u"Library")));
        auto *page = prefs->findChild<LibraryPage *>();
        QVERIFY(page);
        box(*page)->setChecked(false);
        QCOMPARE(told, 0); // not before it is applied
        QVERIFY(prefs->apply());
        QCOMPARE(told, 1);
        QVERIFY(!lib.model()->searchIgnoresAccents()); // at once, the dialog still open
        prefs->accept();
        worked = true;
    };
    actions.action(s(u"edit.preferences"))->trigger();
    QVERIFY(worked);
    // The call of before still works, with no hook.
    installLibraryPreferences(&tools, &background, &settings);
}

QTEST_MAIN(LibraryPageAccentsTest)
#include "tst_ui_librarypage_accents.moc"
