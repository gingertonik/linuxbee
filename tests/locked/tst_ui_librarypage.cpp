// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/library-preferences.md (LP-x). Runs
// on the offscreen platform. No message bus is needed: the background shell
// in the last test is given a connection that does not exist.
#include <QCheckBox>
#include <QComboBox>
#include <QDBusConnection>
#include <QFile>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "actions/actionregistry.h"
#include "library/foldermonitor.h"
#include "library/library.h"
#include "mainwindow/mainwindow.h"
#include "preferences/librarypage.h"
#include "preferences/preferencesdialog.h"
#include "shell/backgroundshell.h"
#include "shell/libraryshell.h"
#include "shell/librarypreferences.h"
#include "shell/playbackshell.h"
#include "shell/toolsshell.h"

using namespace linuxbee;
using namespace linuxbee::ui;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

// A settings file as the next run of the program reads it. Within one run Qt
// hands back the value that was put in (a bool stays a bool); read from disk
// by a later run, every value is text. Copying the file gives the later view.
QString nextRun(QSettings &settings)
{
    static int count = 0;
    settings.sync();
    const QString copy = settings.fileName() + QStringLiteral(".run%1.ini").arg(++count);
    QFile::remove(copy);
    QFile::copy(settings.fileName(), copy);
    return copy;
}

template <typename T> T *need(QWidget &in, const char16_t *name)
{
    T *found = in.findChild<T *>(s(name));
    if (!found)
        QTest::qFail(qPrintable(QStringLiteral("no widget named ") + s(name)), __FILE__, __LINE__);
    return found;
}

#define NEED_WIDGETS(page) \
    do { \
        if (!(page).findChild<QListWidget *>(s(u"monitoredList")) || !(page).findChild<QPushButton *>(s(u"addFolderButton")) \
            || !(page).findChild<QPushButton *>(s(u"removeFolderButton")) || !(page).findChild<QComboBox *>(s(u"sectionCombo")) \
            || !(page).findChild<QCheckBox *>(s(u"checkOnStartupBox"))) \
            QFAIL("the page has not built its widgets"); \
    } while (false)

struct Desk {
    QTemporaryDir dir;
    QSettings settings;
    QString answer; // what the folder chooser will say
    int asked = 0;
    int stored = 0;
    QWidget *askedFrom = nullptr;
    Desk()
        : settings(dir.path() + QStringLiteral("/s.ini"), QSettings::IniFormat)
    {
    }
    LibraryPage::Hooks hooks()
    {
        LibraryPage::Hooks h;
        h.chooseFolder = [this](QWidget *parent) {
            ++asked;
            askedFrom = parent;
            return answer;
        };
        h.stored = [this] { ++stored; };
        return h;
    }
    QStringList rows(LibraryPage &page)
    {
        QStringList out;
        auto *list = page.findChild<QListWidget *>(s(u"monitoredList"));
        for (int i = 0; list && i < list->count(); ++i)
            out.append(list->item(i)->text());
        return out;
    }
};

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

class LibraryPageTest : public QObject {
    Q_OBJECT
private slots:
    void structure();
    void loadsWhatIsStored();
    void addAndRemove();
    void buttons();
    void modified();
    void apply();
    void loadForgets();
    void inTheDialog();
    void installed();
};

void LibraryPageTest::structure()
{
    // LP-STRUCTURE
    Desk k;
    LibraryPage page(&k.settings, k.hooks());
    QCOMPARE(page.title(), s(u"Library"));
    NEED_WIDGETS(page);
    QCOMPARE(k.rows(page), QStringList());
    QCOMPARE(page.folders(), QStringList());
    auto *section = need<QComboBox>(page, u"sectionCombo");
    QStringList offered;
    for (int i = 0; i < section->count(); ++i)
        offered.append(section->itemText(i));
    QCOMPARE(offered, Library::categories());
    QVERIFY(!section->isEditable());
    QCOMPARE(section->currentText(), s(u"Inbox"));
    QVERIFY(!need<QCheckBox>(page, u"checkOnStartupBox")->isChecked());
    QVERIFY(need<QPushButton>(page, u"addFolderButton")->isEnabled());
    QVERIFY(!need<QPushButton>(page, u"removeFolderButton")->isEnabled());
    QVERIFY(!page.isModified());
    QCOMPARE(k.asked, 0);
}

void LibraryPageTest::loadsWhatIsStored()
{
    // LP-LOAD: as a later run reads the file, where every value is text.
    Desk k;
    k.settings.setValue(s(u"library/monitored"), QStringList { s(u"/music/in"), s(u"/music/more/") });
    k.settings.setValue(s(u"library/monitoredCategory"), s(u"Music"));
    k.settings.setValue(s(u"library/checkOnStartup"), true);
    QSettings later(nextRun(k.settings), QSettings::IniFormat);
    LibraryPage page(&later, k.hooks());
    NEED_WIDGETS(page);
    QCOMPARE(k.rows(page), QStringList({ s(u"/music/in"), s(u"/music/more") }));
    QCOMPARE(page.folders(), k.rows(page));
    QCOMPARE(need<QComboBox>(page, u"sectionCombo")->currentText(), s(u"Music"));
    QVERIFY(need<QCheckBox>(page, u"checkOnStartupBox")->isChecked());
    QVERIFY(!page.isModified()); // a folder shown tidied is not an edit

    // One folder is stored as plain text; nonsense means the defaults.
    k.settings.setValue(s(u"library/monitored"), QStringList { s(u"/only/one") });
    k.settings.setValue(s(u"library/monitoredCategory"), s(u"Nonsense"));
    k.settings.setValue(s(u"library/checkOnStartup"), s(u"perhaps"));
    QSettings odd(nextRun(k.settings), QSettings::IniFormat);
    LibraryPage second(&odd, k.hooks());
    QCOMPARE(second.folders(), QStringList({ s(u"/only/one") }));
    QCOMPARE(need<QComboBox>(second, u"sectionCombo")->currentText(), s(u"Inbox"));
    QVERIFY(!need<QCheckBox>(second, u"checkOnStartupBox")->isChecked());
    QVERIFY(!second.isModified());

    // What cannot be a folder is not shown: empty, relative, a second time.
    k.settings.setValue(s(u"library/monitored"), QStringList { s(u"/a"), QString(), s(u"relative/x"), s(u"/a/"), s(u"/b") });
    QSettings mess(nextRun(k.settings), QSettings::IniFormat);
    LibraryPage third(&mess, k.hooks());
    QCOMPARE(third.folders(), QStringList({ s(u"/a"), s(u"/b") }));
}

void LibraryPageTest::addAndRemove()
{
    // LP-ADD, LP-REMOVE
    Desk k;
    LibraryPage page(&k.settings, k.hooks());
    NEED_WIDGETS(page);
    auto *list = need<QListWidget>(page, u"monitoredList");
    QVERIFY(page.addFolder(s(u"/music/in")));
    QVERIFY(page.addFolder(s(u"/music/b/../other//")));
    QCOMPARE(k.rows(page), QStringList({ s(u"/music/in"), s(u"/music/other") })); // tidied, in the order added
    QCOMPARE(list->currentRow(), 1); // the new one is the current one
    QVERIFY(!page.addFolder(QString()));
    QVERIFY(!page.addFolder(s(u"relative/folder")));
    QVERIFY(!page.addFolder(s(u"/music/in/"))); // there already
    QCOMPARE(list->currentRow(), 0); // and shown to be
    QCOMPARE(page.folders().size(), 2);
    QVERIFY(page.addFolder(s(u"/music/in/deeper"))); // a folder inside another is its own entry
    QVERIFY(!page.removeFolder(s(u"/not/listed")));
    QVERIFY(!page.removeFolder(QString()));
    QVERIFY(page.removeFolder(s(u"/music/in/")));
    QCOMPARE(k.rows(page), QStringList({ s(u"/music/other"), s(u"/music/in/deeper") }));
    QCOMPARE(k.asked, 0);
    QCOMPARE(k.settings.allKeys(), QStringList()); // nothing is stored by editing
}

void LibraryPageTest::buttons()
{
    // LP-ADD, LP-REMOVE: the two buttons.
    Desk k;
    LibraryPage page(&k.settings, k.hooks());
    NEED_WIDGETS(page);
    auto *list = need<QListWidget>(page, u"monitoredList");
    auto *add = need<QPushButton>(page, u"addFolderButton");
    auto *remove = need<QPushButton>(page, u"removeFolderButton");
    k.answer = QString(); // the user gave up
    add->click();
    QCOMPARE(k.asked, 1);
    QCOMPARE(k.askedFrom, &page);
    QCOMPARE(page.folders(), QStringList());
    QVERIFY(!remove->isEnabled());
    k.answer = s(u"/music/in/");
    add->click();
    QCOMPARE(k.asked, 2);
    QCOMPARE(page.folders(), QStringList({ s(u"/music/in") }));
    QVERIFY(remove->isEnabled()); // the new row is current
    k.answer = s(u"/music/two");
    add->click();
    list->setCurrentRow(0);
    remove->click();
    QCOMPARE(page.folders(), QStringList({ s(u"/music/two") }));
    list->setCurrentRow(-1);
    QVERIFY(!remove->isEnabled());
    list->setCurrentRow(0);
    remove->click();
    QCOMPARE(page.folders(), QStringList());
    QVERIFY(!remove->isEnabled());
}

void LibraryPageTest::modified()
{
    // LP-MODIFIED: modified means "differs from what is stored", not "was touched".
    Desk k;
    k.settings.setValue(s(u"library/monitored"), s(u"/music/in"));
    LibraryPage page(&k.settings, k.hooks());
    NEED_WIDGETS(page);
    QSignalSpy changed(&page, &PreferencesPage::modifiedChanged);
    auto *section = need<QComboBox>(page, u"sectionCombo");
    auto *check = need<QCheckBox>(page, u"checkOnStartupBox");
    need<QListWidget>(page, u"monitoredList")->setCurrentRow(0); // selecting is not editing
    QVERIFY(!page.isModified());
    QCOMPARE(changed.size(), 0);
    page.addFolder(s(u"/music/two"));
    QVERIFY(page.isModified());
    QCOMPARE(changed.size(), 1);
    page.addFolder(s(u"/music/three")); // still modified: no second signal
    QCOMPARE(changed.size(), 1);
    page.removeFolder(s(u"/music/three"));
    page.removeFolder(s(u"/music/two"));
    QVERIFY(!page.isModified()); // back to what is stored
    QCOMPARE(changed.size(), 2);
    check->setChecked(true);
    QVERIFY(page.isModified());
    check->setChecked(false);
    QVERIFY(!page.isModified());
    section->setCurrentText(s(u"Music"));
    QVERIFY(page.isModified());
    section->setCurrentText(s(u"Inbox"));
    QVERIFY(!page.isModified());
    QCOMPARE(changed.size(), 6);
    // The same folders in another order are another list.
    page.addFolder(s(u"/music/two"));
    page.removeFolder(s(u"/music/in"));
    page.addFolder(s(u"/music/in"));
    QVERIFY(page.isModified());
}

void LibraryPageTest::apply()
{
    // LP-APPLY
    Desk k;
    LibraryPage page(&k.settings, k.hooks());
    NEED_WIDGETS(page);
    QCOMPARE(page.apply(), QString()); // nothing to store
    QCOMPARE(k.stored, 0);
    QCOMPARE(k.settings.allKeys(), QStringList());
    page.addFolder(s(u"/music/in"));
    need<QComboBox>(page, u"sectionCombo")->setCurrentText(s(u"Music"));
    need<QCheckBox>(page, u"checkOnStartupBox")->setChecked(true);
    QSignalSpy changed(&page, &PreferencesPage::modifiedChanged);
    QCOMPARE(page.apply(), QString());
    QCOMPARE(k.stored, 1);
    QVERIFY(!page.isModified());
    QCOMPARE(changed.size(), 1);
    {
        QSettings later(nextRun(k.settings), QSettings::IniFormat);
        QCOMPARE(later.value(s(u"library/monitored")).toStringList(), QStringList({ s(u"/music/in") }));
        QCOMPARE(later.value(s(u"library/monitoredCategory")).toString(), s(u"Music"));
        QCOMPARE(later.value(s(u"library/checkOnStartup")).toString(), s(u"true"));
        LibraryPage again(&later, k.hooks());
        QCOMPARE(again.folders(), QStringList({ s(u"/music/in") }));
        QVERIFY(need<QCheckBox>(again, u"checkOnStartupBox")->isChecked());
    }
    QCOMPARE(page.apply(), QString()); // twice: nothing new
    QCOMPARE(k.stored, 1);
    page.removeFolder(s(u"/music/in"));
    QCOMPARE(page.apply(), QString());
    QCOMPARE(k.stored, 2);
    k.settings.sync();
    QCOMPARE(k.settings.value(s(u"library/monitored")).toStringList(), QStringList());
    // Keys of others in the same group are left alone.
    k.settings.setValue(s(u"library/settleMs"), s(u"500"));
    page.addFolder(s(u"/x"));
    page.apply();
    QCOMPARE(k.settings.value(s(u"library/settleMs")).toString(), s(u"500"));
    // A page with no hooks works too.
    LibraryPage bare(&k.settings);
    bare.addFolder(s(u"/y"));
    QCOMPARE(bare.apply(), QString());
    QCOMPARE(k.settings.value(s(u"library/monitored")).toStringList(), QStringList({ s(u"/x"), s(u"/y") }));
}

void LibraryPageTest::loadForgets()
{
    // LP-LOAD: load() shows what is stored and drops edits not applied.
    Desk k;
    k.settings.setValue(s(u"library/monitored"), s(u"/music/in"));
    LibraryPage page(&k.settings, k.hooks());
    NEED_WIDGETS(page);
    page.addFolder(s(u"/music/two"));
    need<QCheckBox>(page, u"checkOnStartupBox")->setChecked(true);
    QVERIFY(page.isModified());
    QSignalSpy changed(&page, &PreferencesPage::modifiedChanged);
    page.load();
    QCOMPARE(page.folders(), QStringList({ s(u"/music/in") }));
    QVERIFY(!need<QCheckBox>(page, u"checkOnStartupBox")->isChecked());
    QVERIFY(!page.isModified());
    QCOMPARE(changed.size(), 1);
    QCOMPARE(k.stored, 0);
    // And picks up what someone else stored meanwhile.
    k.settings.setValue(s(u"library/monitored"), QStringList { s(u"/a"), s(u"/b") });
    page.load();
    QCOMPARE(page.folders(), QStringList({ s(u"/a"), s(u"/b") }));
    QVERIFY(!page.isModified());
}

void LibraryPageTest::inTheDialog()
{
    // The page behaves as preferences.md asks of any page.
    Desk k;
    PreferencesDialog dialog;
    auto *page = new LibraryPage(&k.settings, k.hooks());
    dialog.addPage(page);
    NEED_WIDGETS(*page);
    QCOMPARE(dialog.pages(), QStringList({ s(u"Library") }));
    QSignalSpy applied(&dialog, &PreferencesDialog::applied);
    page->addFolder(s(u"/music/in"));
    QVERIFY(dialog.apply());
    QCOMPARE(applied.size(), 1);
    QCOMPARE(k.stored, 1);
    QCOMPARE(k.settings.value(s(u"library/monitored")).toStringList(), QStringList({ s(u"/music/in") }));
    page->addFolder(s(u"/music/dropped"));
    dialog.reject(); // PF-CANCEL: edits not applied are gone
    QCOMPARE(page->folders(), QStringList({ s(u"/music/in") }));
    QCOMPARE(k.stored, 1);
}

void LibraryPageTest::installed()
{
    // LP-INSTALL: in the application, applying the page changes what is watched, at once.
    QTemporaryDir dir;
    QDir(dir.path()).mkpath(s(u"playlists"));
    QDir(dir.path()).mkpath(s(u"inbox"));
    const QString inbox = dir.path() + s(u"/inbox");
    QSettings settings(dir.path() + s(u"/s.ini"), QSettings::IniFormat);
    auto library = Library::open(s(u":memory:"));
    PlaylistStore playlists(dir.path() + s(u"/playlists"));
    ActionRegistry actions;
    MainWindow window(&actions, &settings);
    Tape tape;
    LibraryShell lib(&window, &actions, library.get(), &playlists, &settings, ShellHooks { });
    PlaybackShell play(&window, &actions, library.get(), &lib, &tape, &settings, [] { return QDateTime::currentDateTimeUtc(); });
    int presented = 0;
    std::function<void(PreferencesDialog *)> work;
    ToolsShell tools(&window, &actions, library.get(), &lib, &settings, [&](QDialog *dialog) {
        ++presented;
        if (auto *prefs = qobject_cast<PreferencesDialog *>(dialog); prefs && work)
            work(prefs);
    });
    BackgroundShell background(&window, library.get(), &play, &settings, { }, QDBusConnection(s(u"no-such-connection")));
    if (!background.monitor())
        QFAIL("plan 33's background shell is not there");
    QCOMPARE(tools.preferencesPages(), QStringList({ s(u"Hotkeys") }));
    int asked = 0;
    installLibraryPreferences(&tools, &background, &settings, [&](QWidget *) {
        ++asked;
        return inbox;
    });
    QCOMPARE(tools.preferencesPages(), QStringList({ s(u"Hotkeys"), s(u"Library") }));
    QCOMPARE(background.monitor()->watched(), QStringList());

    bool worked = false;
    work = [&](PreferencesDialog *prefs) {
        QVERIFY(prefs->selectPage(s(u"Library")));
        auto *page = prefs->findChild<LibraryPage *>();
        QVERIFY(page);
        QCOMPARE(page->folders(), QStringList());
        need<QPushButton>(*page, u"addFolderButton")->click(); // the chooser handed to install
        QCOMPARE(asked, 1);
        QCOMPARE(page->folders(), QStringList({ inbox }));
        QCOMPARE(background.monitor()->watched(), QStringList()); // not before it is applied
        QVERIFY(prefs->apply());
        QCOMPARE(background.monitor()->watched(), QStringList({ inbox })); // at once, the dialog still open
        prefs->accept();
        worked = true;
    };
    actions.action(s(u"edit.preferences"))->trigger();
    QCOMPARE(presented, 1);
    QVERIFY(worked);
    QCOMPARE(settings.value(s(u"library/monitored")).toStringList(), QStringList({ inbox }));

    // Opened again it shows what was stored; removing the folder stops the watching.
    worked = false;
    work = [&](PreferencesDialog *prefs) {
        auto *page = prefs->findChild<LibraryPage *>();
        QVERIFY(page);
        QCOMPARE(page->folders(), QStringList({ inbox }));
        QVERIFY(page->removeFolder(inbox));
        QVERIFY(prefs->apply());
        prefs->accept();
        worked = true;
    };
    actions.action(s(u"edit.preferences"))->trigger();
    QVERIFY(worked);
    QCOMPARE(background.monitor()->watched(), QStringList());
}

QTEST_MAIN(LibraryPageTest)
#include "tst_ui_librarypage.moc"
