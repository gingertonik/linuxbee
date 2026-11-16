// LOCKED ACCEPTANCE TEST (skins: choosing one in the window, adding one). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/skins.md (SC-x). The skins in
// tests/data/skins are invented. Runs on the offscreen platform.
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QPalette>
#include <QSettings>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QtTest>

#include "actions/actionregistry.h"
#include "mainwindow/mainwindow.h"
#include "skins/skinapplier.h"
#include "skins/skinchoosing.h"

using namespace linuxbee::ui;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

QString fixture(const char *name)
{
    return QStringLiteral(LINUXBEE_TEST_DATA "/skins/") + QLatin1String(name);
}

struct Rig {
    QTemporaryDir dir;
    QString folder; // the skins folder; not made here
    QSettings settings;
    ActionRegistry actions;
    MainWindow window;
    SkinApplier applier;
    std::unique_ptr<SkinChoosing> choosing;
    std::optional<QString> pick; // the answer to chooseSkin
    std::optional<QString> file; // the answer to chooseFile
    QStringList asked; // "skin:CURRENT:A|B", "file"

    Rig()
        : folder(dir.path() + QStringLiteral("/skins"))
        , settings(dir.path() + QStringLiteral("/s.ini"), QSettings::IniFormat)
        , window(&actions, &settings)
        , applier(qApp, &settings)
    {
        SkinChoosing::Hooks hooks;
        hooks.chooseSkin = [this](const QStringList &files, const QString &current) {
            asked.append(QStringLiteral("skin:") + current + QLatin1Char(':') + files.join(QLatin1Char('|')));
            return pick;
        };
        hooks.chooseFile = [this] {
            asked.append(QStringLiteral("file"));
            return file;
        };
        choosing = std::make_unique<SkinChoosing>(&window, &actions, &applier, folder, hooks);
    }
    ~Rig()
    {
        choosing.reset();
        applier.clear(); // the application goes back to its own look for the next test
    }
    // Puts a fixture, or these bytes, at a place below the temporary folder; returns the path.
    QString put(const char *fixtureName, const QString &to)
    {
        const QString target = dir.path() + QLatin1Char('/') + to;
        QDir().mkpath(QFileInfo(target).path());
        if (!QFile::copy(fixture(fixtureName), target))
            QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
        return target;
    }
    QString write(const QByteArray &bytes, const QString &to)
    {
        const QString target = dir.path() + QLatin1Char('/') + to;
        QDir().mkpath(QFileInfo(target).path());
        QFile out(target);
        if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size())
            QTest::qFail("could not write a file", __FILE__, __LINE__);
        return target;
    }
    QString status() { return window.statusBar()->currentMessage(); }
};

#define NEED_PARTS(r) \
    do { \
        if (!(r).actions.isImplemented(s(u"view.skins.choose"))) \
            QFAIL("the skin commands have not been built"); \
    } while (false)

}

class SkinChoosingTest : public QObject {
    Q_OBJECT
private slots:
    void theList();
    void choosing();
    void none();
    void adding();
    void addingRefused();
    void commands();
};

void SkinChoosingTest::theList()
{
    // SC-LIST
    Rig r;
    NEED_PARTS(r);
    QCOMPARE(r.choosing->folder(), r.folder);
    QCOMPARE(r.choosing->skinFiles(), QStringList()); // no folder: none, and it is not made by looking
    QVERIFY(!QFileInfo::exists(r.folder));

    const QString red = r.put("Red Accent.xml", s(u"skins/red accent.xml"));
    const QString grey = r.put("Plain Grey.xml", s(u"skins/Plain Grey.XML")); // the ending in any case
    const QString green = r.put("Green Accent.xml", s(u"skins/Sets/Green Accent.xml")); // one folder down
    const QString apple = r.put("Plain Grey.xml", s(u"skins/Zed/apple.xml"));
    const QString odd = r.write("not a skin at all", s(u"skins/Broken.xml")); // listed: nothing is read to list
    r.put("Compiled Base.xmlc", s(u"skins/Compiled Base.xmlc")); // a compiled skin: not offered
    r.put("Plain Grey.xml", s(u"skins/Sets/Deeper/Too Deep.xml")); // two folders down: not looked at
    r.put("Plain Grey.xml", s(u"skins/.hidden.xml"));
    r.put("Plain Grey.xml", s(u"skins/.cache/Hidden.xml"));
    r.write("x", s(u"skins/notes.txt"));
    QDir().mkpath(r.folder + s(u"/Folder.xml")); // a folder with a skin's name is not a skin
    // By the file's name without its ending, case ignored; the folder plays no part in the order.
    QCOMPARE(r.choosing->skinFiles(), QStringList({ apple, odd, green, grey, red }));

    // Two of one name: the one nearer the top first, then by path.
    const QString second = r.put("Red Accent.xml", s(u"skins/Sets/Plain Grey.xml"));
    QCOMPARE(r.choosing->skinFiles(), QStringList({ apple, odd, green, grey, second, red }));
}

void SkinChoosingTest::choosing()
{
    // SC-CHOOSE
    Rig r;
    NEED_PARTS(r);
    QSignalSpy changed(&r.applier, &SkinApplier::changed);

    // With no skin to offer, nothing is asked.
    r.pick = s(u"anything");
    QVERIFY(!r.choosing->choose());
    QCOMPARE(r.asked, QStringList());
    QCOMPARE(r.choosing->lastMessage(), s(u"There are no skins yet. Add one with View > Skins > Add Skin\u2026"));
    QCOMPARE(r.status(), r.choosing->lastMessage());

    const QString grey = r.put("Plain Grey.xml", s(u"skins/Plain Grey.xml"));
    const QString red = r.put("Red Accent.xml", s(u"skins/Red Accent.xml"));
    const QString odd = r.write("not a skin at all", s(u"skins/Broken.xml"));
    const QString all = odd + QLatin1Char('|') + grey + QLatin1Char('|') + red;

    r.pick = grey;
    QVERIFY(r.choosing->choose());
    QCOMPARE(r.asked, QStringList({ s(u"skin::") + all })); // asked once; no skin was in use
    QCOMPARE(r.applier.currentFile(), grey);
    QCOMPARE(changed.count(), 1); // applied once
    QCOMPARE(r.settings.value(s(u"appearance/skin")).toString(), grey);
    QCOMPARE(r.choosing->lastMessage(), s(u"Skin: Plain Grey"));
    QCOMPARE(r.status(), r.choosing->lastMessage());

    // Another one: the question says which is in use.
    r.asked.clear();
    r.pick = red;
    QVERIFY(r.choosing->choose());
    QCOMPARE(r.asked, QStringList({ s(u"skin:") + grey + QLatin1Char(':') + all }));
    QCOMPARE(r.applier.currentFile(), red);
    QCOMPARE(r.applier.basis(), s(u"read")); // it is laid over the grey one beside it
    QCOMPARE(r.choosing->lastMessage(), s(u"Skin: Red Accent"));

    // One that cannot be used: said, and everything stays.
    const QPalette before = qApp->palette();
    r.pick = odd;
    QVERIFY(!r.choosing->choose());
    QCOMPARE(r.applier.currentFile(), red);
    QCOMPARE(qApp->palette(), before);
    QCOMPARE(r.settings.value(s(u"appearance/skin")).toString(), red);
    QVERIFY2(r.choosing->lastMessage().startsWith(s(u"Could not use the skin Broken: ")), qPrintable(r.choosing->lastMessage()));
    QCOMPARE(r.choosing->lastMessage(), s(u"Could not use the skin Broken: ") + r.applier.lastError());
    QVERIFY(!r.applier.lastError().isEmpty());
    QCOMPARE(r.status(), r.choosing->lastMessage());

    // No answer: nothing, and the message stays.
    const QString said = r.choosing->lastMessage();
    const int changes = changed.count();
    r.pick = std::nullopt;
    QVERIFY(!r.choosing->choose());
    QCOMPARE(r.choosing->lastMessage(), said);
    QCOMPARE(changed.count(), changes);
    QCOMPARE(r.applier.currentFile(), red);
}

void SkinChoosingTest::none()
{
    // SC-NONE
    Rig r;
    NEED_PARTS(r);
    const QPalette own = qApp->palette();
    QVERIFY(!r.choosing->none()); // no skin in use: nothing
    QCOMPARE(r.choosing->lastMessage(), QString());
    const QString grey = r.put("Plain Grey.xml", s(u"skins/Plain Grey.xml"));
    r.pick = grey;
    QVERIFY(r.choosing->choose());
    QVERIFY(qApp->palette() != own);
    QVERIFY(r.choosing->none());
    QCOMPARE(r.applier.currentFile(), QString());
    QCOMPARE(qApp->palette(), own);
    QVERIFY(!r.settings.contains(s(u"appearance/skin")));
    QCOMPARE(r.choosing->lastMessage(), s(u"Skin: none"));
    QCOMPARE(r.status(), r.choosing->lastMessage());
    QCOMPARE(r.asked.size(), 1); // none() asks nothing
}

void SkinChoosingTest::adding()
{
    // SC-ADD: the file is copied into the skins folder, with what it is laid over, and used.
    Rig r;
    NEED_PARTS(r);
    const QString from = r.put("Red Accent.xml", s(u"downloads/Red Accent.xml"));
    r.put("Plain Grey.xml", s(u"downloads/Plain Grey.xml")); // what it is laid over, beside it
    r.put("Green Accent.xml", s(u"downloads/Green Accent.xml")); // beside it and not asked for
    r.file = from;
    QVERIFY(r.choosing->add());
    QCOMPARE(r.asked, QStringList({ s(u"file") }));
    const QString red = r.folder + s(u"/Red Accent.xml");
    const QString grey = r.folder + s(u"/Plain Grey.xml");
    QCOMPARE(r.choosing->skinFiles(), QStringList({ grey, red })); // the folder was made; nothing else came
    QCOMPARE(r.applier.currentFile(), red);
    QCOMPARE(r.applier.basis(), s(u"read"));
    QCOMPARE(r.choosing->lastMessage(), s(u"Added the skin Red Accent"));
    QCOMPARE(r.status(), r.choosing->lastMessage());
    QVERIFY(QFileInfo::exists(from)); // copied, not moved

    // One whose base is not beside it: added alone.
    const QString lone = r.put("Green Accent.xml", s(u"elsewhere/Green Accent.xml"));
    r.file = lone;
    QVERIFY(r.choosing->add());
    const QString green = r.folder + s(u"/Green Accent.xml");
    QCOMPARE(r.choosing->skinFiles(), QStringList({ green, grey, red }));
    QCOMPARE(r.applier.currentFile(), green);
    QCOMPARE(r.applier.basis(), s(u"unreadable"));
    QVERIFY(!QFileInfo::exists(r.folder + s(u"/Compiled Base.xmlc")));

    // A base that is beside it is copied even when linuxBee cannot read it; one that is there already is left.
    QVERIFY(QFile::remove(green));
    r.put("Compiled Base.xmlc", s(u"elsewhere/Compiled Base.xmlc"));
    QVERIFY(r.choosing->add());
    QVERIFY(QFileInfo::exists(r.folder + s(u"/Compiled Base.xmlc")));
    QVERIFY(QFile::remove(green));
    r.write("mine", s(u"skins/Compiled Base.xmlc"));
    QVERIFY(r.choosing->add());
    QFile kept(r.folder + s(u"/Compiled Base.xmlc"));
    QVERIFY(kept.open(QIODevice::ReadOnly));
    QCOMPARE(kept.readAll(), QByteArray("mine"));
}

void SkinChoosingTest::addingRefused()
{
    // SC-ADD: what is refused, and that nothing changes then.
    Rig r;
    NEED_PARTS(r);
    QSignalSpy changed(&r.applier, &SkinApplier::changed);

    r.file = std::nullopt; // gave up
    QVERIFY(!r.choosing->add());
    QCOMPARE(r.choosing->lastMessage(), QString());
    QVERIFY(!QFileInfo::exists(r.folder));

    r.file = r.write("not a skin at all", s(u"downloads/Broken.xml"));
    QVERIFY(!r.choosing->add());
    QVERIFY2(r.choosing->lastMessage().startsWith(s(u"Could not add the skin Broken: ")), qPrintable(r.choosing->lastMessage()));
    QVERIFY(r.choosing->lastMessage().size() > 31);
    QCOMPARE(r.status(), r.choosing->lastMessage());
    QCOMPARE(r.choosing->skinFiles(), QStringList());

    r.file = r.dir.path() + s(u"/downloads/Missing.xml");
    QVERIFY(!r.choosing->add());
    QVERIFY(r.choosing->lastMessage().startsWith(s(u"Could not add the skin Missing: ")));

    r.file = r.put("Compiled Base.xmlc", s(u"downloads/Compiled Base.xmlc"));
    QVERIFY(!r.choosing->add());
    QCOMPARE(r.choosing->lastMessage(), s(u"Could not add the skin Compiled Base: linuxBee cannot read compiled skins (.xmlc)"));

    QFile grey(fixture("Plain Grey.xml"));
    QVERIFY(grey.open(QIODevice::ReadOnly));
    r.file = r.write(grey.readAll(), s(u"downloads/Grey.txt")); // a skin inside, and not a skin's ending
    QVERIFY(!r.choosing->add());
    QCOMPARE(r.choosing->lastMessage(), s(u"Could not add the skin Grey: a skin file ends in .xml"));
    QCOMPARE(r.choosing->skinFiles(), QStringList());
    QVERIFY(!QFileInfo::exists(r.folder));

    // A name the folder has already, in any case: refused, and the one there is kept.
    r.write("mine", s(u"skins/plain grey.xml"));
    r.file = r.put("Plain Grey.xml", s(u"downloads/Plain Grey.xml"));
    QVERIFY(!r.choosing->add());
    QCOMPARE(r.choosing->lastMessage(), s(u"Could not add the skin Plain Grey: there is a skin of that name already"));
    QFile kept(r.folder + s(u"/plain grey.xml"));
    QVERIFY(kept.open(QIODevice::ReadOnly));
    QCOMPARE(kept.readAll(), QByteArray("mine"));
    QCOMPARE(r.choosing->skinFiles().size(), 1);

    // A file that is in the skins folder already.
    r.file = r.put("Red Accent.xml", s(u"skins/Red Accent.xml"));
    QVERIFY(!r.choosing->add());
    QCOMPARE(r.choosing->lastMessage(), s(u"Could not add the skin Red Accent: there is a skin of that name already"));

    QCOMPARE(changed.count(), 0); // none of this touched the look
    QCOMPARE(r.applier.currentFile(), QString());
}

void SkinChoosingTest::commands()
{
    // SC-WIRING
    Rig r;
    NEED_PARTS(r);
    for (const char16_t *id : { u"view.skins.choose", u"view.skins.add", u"view.skins.none" }) {
        QVERIFY2(r.actions.action(s(id)), qPrintable(s(id)));
        QVERIFY2(r.actions.isImplemented(s(id)), qPrintable(s(id)));
    }
    const QString grey = r.put("Plain Grey.xml", s(u"skins/Plain Grey.xml"));
    r.pick = grey;
    r.actions.action(s(u"view.skins.choose"))->trigger();
    QCOMPARE(r.applier.currentFile(), grey);
    r.actions.action(s(u"view.skins.none"))->trigger();
    QCOMPARE(r.applier.currentFile(), QString());
    r.file = r.put("Red Accent.xml", s(u"downloads/Red Accent.xml"));
    r.actions.action(s(u"view.skins.add"))->trigger();
    QCOMPARE(r.applier.currentFile(), r.folder + s(u"/Red Accent.xml"));
    QCOMPARE(r.asked.size(), 2);
    // Destroying the object frees the commands.
    r.choosing.reset();
    for (const char16_t *id : { u"view.skins.choose", u"view.skins.add", u"view.skins.none" })
        QVERIFY2(!r.actions.isImplemented(s(id)), qPrintable(s(id)));
}

QTEST_MAIN(SkinChoosingTest)
#include "tst_ui_skinchoosing.moc"
