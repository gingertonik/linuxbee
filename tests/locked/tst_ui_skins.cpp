// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/skins.md (SK-x). The skins in
// tests/data/skins are invented. Runs on the offscreen platform.
#include <QApplication>
#include <QFile>
#include <QPalette>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "skin/skin.h"
#include "skins/skinapplier.h"

using namespace linuxbee;
using linuxbee::ui::SkinApplier;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

QString path(const char *name)
{
    return QStringLiteral(LINUXBEE_TEST_DATA "/skins/") + QLatin1String(name);
}

QByteArray bytes(const char *name)
{
    QFile file(path(name));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

SkinColour c(int r, int g, int b, int a = 255)
{
    return { r, g, b, a };
}

QByteArray skin(const QByteArray &inside, const QByteArray &rootAttributes = "")
{
    return "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n<root" + rootAttributes + ">\r\n" + inside + "</root>\r\n";
}

QColor q(const SkinColour &colour)
{
    return QColor(colour.r, colour.g, colour.b, colour.a);
}

// A settings file as the next run of the program reads it (every value text).
QString nextRun(QSettings &settings)
{
    static int count = 0;
    settings.sync();
    const QString copy = settings.fileName() + QStringLiteral(".run%1.ini").arg(++count);
    QFile::remove(copy);
    QFile::copy(settings.fileName(), copy);
    return copy;
}

QStringList warnings;
void collect(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    if (type == QtWarningMsg || type == QtCriticalMsg)
        warnings.append(message);
}

}

class SkinsTest : public QObject {
    Q_OBJECT
private slots:
    void reading();
    void readingOddFiles();
    void layering();
    void colours();
    void sheet();
    void applying();
    void dependent();
    void stored();
};

void SkinsTest::reading()
{
    // SK-READ
    const Skin plain = readSkin(bytes("Plain Grey.xml"));
    QVERIFY2(plain.ok, qPrintable(plain.error));
    QCOMPARE(plain.dependsOn, QString());
    QCOMPARE(plain.variables.size(), 3);
    QCOMPARE(plain.variables.value(s(u"MainBG")), c(250, 250, 250));
    QCOMPARE(plain.variables.value(s(u"MainFG")), c(20, 20, 20));
    QCOMPARE(plain.variables.value(s(u"accent")), c(30, 120, 200));
    QCOMPARE(plain.elements.size(), 23); // in the three sections alike
    SkinElement body;
    body.bg = c(250, 250, 250); // by a variable
    body.fg = c(21, 21, 21); // from the second element of that id
    body.bdr = c(180, 180, 180);
    QCOMPARE(plain.elements.value(s(u"Panel.Body.Default")), body);
    const SkinElement header = plain.elements.value(s(u"Panel.ChildHeader.Default"));
    QCOMPARE(header.bg2, std::optional<SkinColour>(c(205, 205, 205)));
    QVERIFY(!header.fg2.has_value());
    const SkinElement wave = plain.elements.value(s(u"Player.Wavebar"));
    QCOMPARE(wave.fg, std::optional<SkinColour>(c(30, 120, 200)));
    QCOMPARE(wave.fg2, std::optional<SkinColour>(c(25, 25, 25, 128))); // four numbers: with an alpha
    QVERIFY(!wave.bg.has_value());
    QCOMPARE(plain.elements.value(s(u"FullNodeSelect")).text, s(u"true"));
    QCOMPARE(plain.elements.value(s(u"MainPanelMargin.Left")).text, s(u"5")); // trimmed
    QCOMPARE(plain.elements.value(s(u"WaitIndicator")).text, s(u"R0lGODlhAQABAAAAACw="));
    const SkinElement odd = plain.elements.value(s(u"Odd.One"));
    QVERIFY(plain.elements.contains(s(u"Odd.One")));
    QVERIFY(!odd.bg.has_value() && !odd.fg.has_value() && !odd.bdr.has_value());
    QCOMPARE(plain.problems,
        QStringList({ s(u"variable broken: cannot read \"300,1,1\""), s(u"Odd.One bg: cannot read \"nosuchname\""),
            s(u"Odd.One fg: cannot read \"1,2\"") }));

    const Skin red = readSkin(bytes("Red Accent.xml"));
    QVERIFY(red.ok);
    QCOMPARE(red.dependsOn, s(u"Plain Grey.xml"));
    QCOMPARE(red.elements.size(), 4); // elements straight under the root
    QCOMPARE(red.elements.value(s(u"Panel.Body.Highlight")).bg, std::optional<SkinColour>(c(160, 40, 40)));
    QCOMPARE(red.problems, QStringList());
}

void SkinsTest::readingOddFiles()
{
    // SK-READ: what is and is not a skin, and the edges of a colour.
    for (const QByteArray &bad : { QByteArray(), QByteArray("hello"), bytes("Compiled Base.xmlc"),
             QByteArray("<?xml version=\"1.0\"?>\r\n<other><element id=\"A\" bg=\"1,2,3\" /></other>\r\n"),
             QByteArray("<?xml version=\"1.0\"?>\r\n<root>\r\n<element id=\"A\" bg=\"1,2,3\" />\r\n"), bytes("Plain Grey.xml").left(900) }) {
        const Skin broken = readSkin(bad);
        QVERIFY2(!broken.ok && !broken.error.isEmpty(), bad.left(40).constData());
        QCOMPARE(skinColours(broken).size(), 0);
    }
    QVERIFY(bytes("Compiled Base.xmlc").size() == 20);
    Skin a = readSkin("\xEF\xBB\xBF" + skin(""));
    QVERIFY2(a.ok, qPrintable(a.error));
    QVERIFY(a.elements.isEmpty() && a.variables.isEmpty() && a.problems.isEmpty());
    a = readSkin(skin("<variables>\n  late = \"1,2,3\"\n</variables>\n", " dependsOn=\"  Base Skin.xml \" other=\"x\""));
    QCOMPARE(a.dependsOn, s(u"Base Skin.xml"));
    a = readSkin(skin("<element id=\"A\" bg=\" 0 , 255 ,7 \" fg=\"0,0,0,0\" bdr=\"later\" />\r\n"
                      "<element id=\"B\" bg=\"256,0,0\" fg=\"-1,0,0\" bdr=\"1,2,3,4,5\" />\r\n"
                      "<element id=\"C\" bg=\"1.5,0,0\" fg=\"a,b,c\" bg2=\"1,,3\" fg2=\"0x10,0,0\" />\r\n"
                      "<element id=\"\" bg=\"1,2,3\" />\r\n<element bg=\"1,2,3\" />\r\n<xelement id=\"X\" bg=\"1,2,3\" />\r\n"
                      "<element id=\"T\">first</element>\r\n<element id=\"T\" bg=\"9,9,9\"></element>\r\n<element id=\"T\"> second </element>\r\n"
                      "<variables>\r\nlater = \"4,5,6\"\r\n A-b_1=\"7,8,9,10\"\r\nbad name = \"1,2,3\"\r\nunquoted = 1,2,3\r\n</variables>\r\n"));
    QVERIFY(a.ok);
    QCOMPARE(a.elements.keys(), QStringList({ s(u"A"), s(u"B"), s(u"C"), s(u"T") })); // no id, an empty id, another tag: passed over
    QCOMPARE(a.elements.value(s(u"A")).bg, std::optional<SkinColour>(c(0, 255, 7)));
    QCOMPARE(a.elements.value(s(u"A")).fg, std::optional<SkinColour>(c(0, 0, 0, 0)));
    QCOMPARE(a.elements.value(s(u"A")).bdr, std::optional<SkinColour>(c(4, 5, 6))); // a variable defined further down
    QCOMPARE(a.variables.keys(), QStringList({ s(u"A-b_1"), s(u"later") }));
    QCOMPARE(a.variables.value(s(u"A-b_1")), c(7, 8, 9, 10));
    const SkinElement t = a.elements.value(s(u"T"));
    QCOMPARE(t.text, s(u"second")); // an empty text does not replace
    QCOMPARE(t.bg, std::optional<SkinColour>(c(9, 9, 9)));
    for (const char16_t *id : { u"B", u"C" }) {
        const SkinElement e = a.elements.value(s(id));
        QVERIFY(!e.bg && !e.bg2 && !e.fg && !e.fg2 && !e.bdr);
    }
    QCOMPARE(a.problems.size(), 7); // three of B, four of C; no problem for a line that is not a variable
    QVERIFY(a.problems.contains(s(u"B bg: cannot read \"256,0,0\"")));
    QVERIFY(a.problems.contains(s(u"C fg2: cannot read \"0x10,0,0\"")));
}

void SkinsTest::layering()
{
    // SK-LAYER
    const Skin plain = readSkin(bytes("Plain Grey.xml"));
    const Skin red = readSkin(bytes("Red Accent.xml"));
    const Skin both = layeredSkin(plain, red);
    QVERIFY(both.ok);
    QCOMPARE(both.dependsOn, QString()); // as the base has it
    QCOMPARE(both.elements.size(), 24); // one id is new
    SkinElement highlight;
    highlight.bg = c(160, 40, 40); // from the top
    highlight.fg = c(255, 250, 250);
    highlight.bdr = c(30, 120, 200); // the top has none: the base's stays
    QCOMPARE(both.elements.value(s(u"Panel.Body.Highlight")), highlight);
    QCOMPARE(both.elements.value(s(u"Panel.Body.Default")), plain.elements.value(s(u"Panel.Body.Default")));
    QCOMPARE(both.variables.size(), 5);
    QCOMPARE(both.variables.value(s(u"theme")), c(160, 40, 40));
    QCOMPARE(both.problems, plain.problems);
    // A side that is not ok.
    const Skin broken = readSkin(bytes("Compiled Base.xmlc"));
    const Skin alone = layeredSkin(broken, red);
    QVERIFY(alone.ok);
    QCOMPARE(alone.elements, red.elements);
    QCOMPARE(alone.dependsOn, red.dependsOn);
    QVERIFY(!layeredSkin(plain, broken).ok);
    QVERIFY(!layeredSkin(broken, broken).ok);
}

void SkinsTest::colours()
{
    // SK-COLOURS
    const QMap<QString, SkinColour> plain = skinColours(readSkin(bytes("Plain Grey.xml")));
    const QMap<QString, SkinColour> want {
        { s(u"window"), c(230, 230, 230) }, { s(u"windowText"), c(21, 21, 21) }, { s(u"base"), c(250, 250, 250) },
        { s(u"text"), c(21, 21, 21) }, { s(u"alternateBase"), c(243, 243, 243) }, { s(u"dimText"), c(120, 120, 120) },
        { s(u"highlight"), c(30, 120, 200) }, { s(u"highlightText"), c(255, 255, 255) }, { s(u"button"), c(225, 225, 225) },
        { s(u"buttonText"), c(0, 0, 0) }, { s(u"header"), c(220, 220, 220) }, { s(u"headerText"), c(40, 40, 40) },
        { s(u"menuBar"), c(210, 210, 210) }, { s(u"menuBarText"), c(10, 10, 10) }, { s(u"menu"), c(245, 245, 245) },
        { s(u"menuText"), c(20, 20, 20) }, { s(u"menuHighlight"), c(30, 120, 200) }, { s(u"menuHighlightText"), c(255, 255, 255) },
        { s(u"statusBar"), c(215, 215, 215) }, { s(u"statusBarText"), c(60, 60, 60) }, { s(u"player"), c(60, 60, 60) },
        { s(u"playerText"), c(240, 240, 240) }, { s(u"tab"), c(215, 215, 215) }, { s(u"tabText"), c(50, 50, 50) },
        { s(u"tabSelected"), c(250, 250, 250) }, { s(u"tabSelectedText"), c(30, 120, 200) },
    };
    QCOMPARE(plain.keys(), want.keys());
    for (auto it = want.cbegin(); it != want.cend(); ++it)
        QVERIFY2(plain.value(it.key()) == it.value(), qPrintable(it.key() + QLatin1Char(' ') + skinColourText(plain.value(it.key()))));
    // The small skin alone has only what it names.
    const QMap<QString, SkinColour> red = skinColours(readSkin(bytes("Red Accent.xml")));
    QCOMPARE(red.keys(),
        QStringList({ s(u"highlight"), s(u"highlightText"), s(u"menuHighlight"), s(u"menuHighlightText"), s(u"tabSelectedText") }));
    QCOMPARE(red.value(s(u"menuHighlightText")), c(255, 250, 250)); // its own element, before the fallback
    // The first source wins; the second is used only without the first.
    Skin order = readSkin(skin("<element id=\"ApplicationBackColour\" bg=\"1,1,1\" />\r\n<element id=\"Panel.Header.Default\" bg=\"2,2,2\" />\r\n"
                               "<element id=\"Panel.ChildHeader.Default\" bg=\"3,3,3\" fg=\"4,4,4\" />\r\n"
                               "<element id=\"PlayerSidebar.Background\" bg=\"5,5,5\" fg=\"6,6,6\" />\r\n"
                               "<element id=\"PlayerFlat.Background\" fg=\"7,7,7\" />\r\n<element id=\"WindowBackColour\" fg=\"8,8,8\" />\r\n"));
    const QMap<QString, SkinColour> got = skinColours(order);
    QCOMPARE(got.value(s(u"window")), c(1, 1, 1)); // WindowBackColour has no bg
    QCOMPARE(got.value(s(u"header")), c(2, 2, 2));
    QCOMPARE(got.value(s(u"headerText")), c(4, 4, 4));
    QCOMPARE(got.value(s(u"player")), c(5, 5, 5));
    QCOMPARE(got.value(s(u"playerText")), c(7, 7, 7));
    QCOMPARE(got.size(), 5);
    QCOMPARE(skinColourText(c(1, 22, 255)), s(u"1,22,255"));
    QCOMPARE(skinColourText(c(1, 22, 255, 0)), s(u"1,22,255,0"));
    QCOMPARE(skinColours(Skin()).size(), 0);
}

void SkinsTest::sheet()
{
    // SK-SHEET
    QCOMPARE(skinStyleSheet({ }), QString());
    QCOMPARE(skinStyleSheet(skinColours(readSkin(bytes("Plain Grey.xml")))),
        s(u"QHeaderView::section { background-color: rgb(220,220,220); color: rgb(40,40,40); }\n"
          u"QMenuBar { background-color: rgb(210,210,210); color: rgb(10,10,10); }\n"
          u"QMenu { background-color: rgb(245,245,245); color: rgb(20,20,20); }\n"
          u"QMenu::item:selected { background-color: rgb(30,120,200); color: rgb(255,255,255); }\n"
          u"QStatusBar { background-color: rgb(215,215,215); color: rgb(60,60,60); }\n"
          u"#playerBar { background-color: rgb(60,60,60); color: rgb(240,240,240); } #playerBar QLabel { color: rgb(240,240,240); }\n"
          u"QTabBar::tab { background-color: rgb(215,215,215); color: rgb(50,50,50); padding: 4px 10px; }\n"
          u"QTabBar::tab:selected { background-color: rgb(250,250,250); color: rgb(30,120,200); }"));
    // A line needs every name it uses; palette names add nothing.
    QCOMPARE(skinStyleSheet(skinColours(readSkin(bytes("Red Accent.xml")))),
        s(u"QMenu::item:selected { background-color: rgb(160,40,40); color: rgb(255,250,250); }"));
    QCOMPARE(skinStyleSheet({ { s(u"menu"), c(1, 2, 3) }, { s(u"window"), c(4, 5, 6) }, { s(u"base"), c(7, 8, 9) } }), QString());
    QCOMPARE(skinStyleSheet({ { s(u"statusBar"), c(1, 2, 3, 128) }, { s(u"statusBarText"), c(4, 5, 6) } }),
        s(u"QStatusBar { background-color: rgba(1,2,3,128); color: rgb(4,5,6); }"));
}

void SkinsTest::applying()
{
    // SK-APPLY
    QTemporaryDir dir;
    QSettings settings(dir.path() + s(u"/s.ini"), QSettings::IniFormat);
    const QPalette own = qApp->palette();
    const QString ownSheet = qApp->styleSheet();
    {
        SkinApplier applier(qApp, &settings);
        if (applier.currentFile() == s(u"?"))
            QFAIL("the skin applier has not been built");
        QCOMPARE(applier.currentFile(), QString());
        QCOMPARE(applier.basis(), QString());
        QSignalSpy changed(&applier, &SkinApplier::changed);
        applier.clear(); // nothing applied: nothing
        QCOMPARE(changed.count(), 0);

        warnings.clear();
        const QtMessageHandler before = qInstallMessageHandler(collect);
        const bool applied = applier.apply(path("Plain Grey.xml"));
        qInstallMessageHandler(before);
        QVERIFY2(applied, qPrintable(applier.lastError()));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QLatin1Char('|')))); // Qt takes the sheet without complaint
        QCOMPARE(changed.count(), 1);
        QCOMPARE(applier.currentFile(), path("Plain Grey.xml"));
        QCOMPARE(applier.basis(), s(u"none"));
        const QPalette now = qApp->palette();
        QCOMPARE(now.color(QPalette::Window), q(c(230, 230, 230)));
        QCOMPARE(now.color(QPalette::WindowText), q(c(21, 21, 21)));
        QCOMPARE(now.color(QPalette::Base), q(c(250, 250, 250)));
        QCOMPARE(now.color(QPalette::AlternateBase), q(c(243, 243, 243)));
        QCOMPARE(now.color(QPalette::Text), q(c(21, 21, 21)));
        QCOMPARE(now.color(QPalette::PlaceholderText), q(c(120, 120, 120)));
        QCOMPARE(now.color(QPalette::Highlight), q(c(30, 120, 200)));
        QCOMPARE(now.color(QPalette::HighlightedText), q(c(255, 255, 255)));
        QCOMPARE(now.color(QPalette::Button), q(c(225, 225, 225)));
        QCOMPARE(now.color(QPalette::ButtonText), q(c(0, 0, 0)));
        QCOMPARE(now.color(QPalette::ToolTipBase), own.color(QPalette::ToolTipBase)); // not one of ours: left alone
        QCOMPARE(qApp->styleSheet(), skinStyleSheet(skinColours(readSkin(bytes("Plain Grey.xml")))));
        QSettings later(nextRun(settings), QSettings::IniFormat);
        QCOMPARE(later.value(s(u"appearance/skin")).toString(), path("Plain Grey.xml")); // stored at once

        // Refused: nothing changes.
        for (const QString &bad : { path("Compiled Base.xmlc"), path("no-such-skin.xml"), dir.path() }) {
            QVERIFY(!applier.apply(bad));
            QVERIFY(!applier.lastError().isEmpty());
            QCOMPARE(applier.currentFile(), path("Plain Grey.xml"));
            QCOMPARE(qApp->palette().color(QPalette::Base), q(c(250, 250, 250)));
        }
        QCOMPARE(changed.count(), 1);
        QCOMPARE(settings.value(s(u"appearance/skin")).toString(), path("Plain Grey.xml"));

        applier.clear();
        QCOMPARE(changed.count(), 2);
        QCOMPARE(applier.currentFile(), QString());
        QCOMPARE(applier.basis(), QString());
        QCOMPARE(qApp->palette(), own);
        QCOMPARE(qApp->styleSheet(), QString());
        QVERIFY(!settings.contains(s(u"appearance/skin")));
    }
    QCOMPARE(qApp->palette(), own);
    qApp->setStyleSheet(ownSheet);
}

void SkinsTest::dependent()
{
    // SK-APPLY, step 2.
    QTemporaryDir dir;
    QSettings settings(dir.path() + s(u"/s.ini"), QSettings::IniFormat);
    const QPalette own = qApp->palette();
    SkinApplier applier(qApp, &settings);
    if (applier.currentFile() == s(u"?"))
        QFAIL("the skin applier has not been built");
    // Over a skin that can be read: both.
    QVERIFY2(applier.apply(path("Red Accent.xml")), qPrintable(applier.lastError()));
    QCOMPARE(applier.basis(), s(u"read"));
    QCOMPARE(applier.currentFile(), path("Red Accent.xml"));
    QCOMPARE(qApp->palette().color(QPalette::Highlight), q(c(160, 40, 40)));
    QCOMPARE(qApp->palette().color(QPalette::Base), q(c(250, 250, 250))); // from the base
    QVERIFY(qApp->styleSheet().contains(s(u"QTabBar::tab:selected { background-color: rgb(250,250,250); color: rgb(160,40,40); }")));
    QVERIFY(qApp->styleSheet().contains(s(u"QStatusBar")));
    // Over a compiled skin: the small skin alone, over linuxBee's own look.
    QVERIFY2(applier.apply(path("Green Accent.xml")), qPrintable(applier.lastError()));
    QCOMPARE(applier.basis(), s(u"unreadable"));
    QCOMPARE(qApp->palette().color(QPalette::Highlight), q(c(40, 140, 60)));
    QCOMPARE(qApp->palette().color(QPalette::Base), own.color(QPalette::Base)); // not the earlier skin's
    QCOMPARE(qApp->palette().color(QPalette::Window), own.color(QPalette::Window));
    QCOMPARE(qApp->styleSheet(), s(u"QMenu::item:selected { background-color: rgb(40,140,60); color: rgb(255,250,250); }"));
    // The base is missing altogether: the same.
    QVERIFY(QFile::copy(path("Red Accent.xml"), dir.path() + s(u"/Lonely.xml")));
    QVERIFY(applier.apply(dir.path() + s(u"/Lonely.xml")));
    QCOMPARE(applier.basis(), s(u"unreadable"));
    QCOMPARE(qApp->palette().color(QPalette::Highlight), q(c(160, 40, 40)));
    QCOMPARE(qApp->palette().color(QPalette::Base), own.color(QPalette::Base));
    applier.clear();
    QCOMPARE(qApp->palette(), own);
}

void SkinsTest::stored()
{
    // SK-APPLY: what a later start does.
    QTemporaryDir dir;
    QSettings settings(dir.path() + s(u"/s.ini"), QSettings::IniFormat);
    const QPalette own = qApp->palette();
    {
        SkinApplier first(qApp, &settings);
        if (first.currentFile() == s(u"?"))
            QFAIL("the skin applier has not been built");
        QVERIFY(first.apply(path("Red Accent.xml")));
        first.clear();
        QVERIFY(first.apply(path("Plain Grey.xml")));
    }
    qApp->setPalette(own); // a new start begins with linuxBee's own look
    qApp->setStyleSheet(QString());
    QSettings later(nextRun(settings), QSettings::IniFormat);
    {
        SkinApplier next(qApp, &later);
        QCOMPARE(next.currentFile(), path("Plain Grey.xml"));
        QCOMPARE(next.basis(), s(u"none"));
        QCOMPARE(qApp->palette().color(QPalette::Base), q(c(250, 250, 250)));
        QVERIFY(!qApp->styleSheet().isEmpty());
        next.clear();
    }
    QCOMPARE(qApp->palette(), own);
    // A stored file that is gone: own look, and the path is kept.
    later.setValue(s(u"appearance/skin"), dir.path() + s(u"/gone.xml"));
    {
        SkinApplier next(qApp, &later);
        QCOMPARE(next.currentFile(), QString());
        QVERIFY(!next.lastError().isEmpty());
        QCOMPARE(qApp->palette(), own);
        QCOMPARE(qApp->styleSheet(), QString());
        QCOMPARE(later.value(s(u"appearance/skin")).toString(), dir.path() + s(u"/gone.xml"));
    }
    later.setValue(s(u"appearance/skin"), QString());
    {
        SkinApplier next(qApp, &later);
        QCOMPARE(next.currentFile(), QString());
        QCOMPARE(next.lastError(), QString()); // nothing stored is no failure
        QCOMPARE(qApp->palette(), own);
    }
}

QTEST_MAIN(SkinsTest)
#include "tst_ui_skins.moc"
