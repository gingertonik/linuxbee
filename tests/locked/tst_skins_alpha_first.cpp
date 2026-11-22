// LOCKED ACCEPTANCE TEST (a colour of four numbers puts its alpha first). Written by the reviewer,
// not the builder. The builder makes these pass by changing src/, never by editing this file. A
// test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/skins.md (SK-READ, SK-SHEET). C-KRY: the community skin
// "Kryptonite Dark" 1.2 by Ingo_Igel (forum topic 35252; Unlicense) has the variables
// gray_3="43,43,43" and gray_3_iconbackground="128,43,43,43", the same grey at half opacity, and
// "255,0,255,0" for its opaque green 0,255,0. The skin text here is written for this test, in that
// shape; no file of that skin is read.
#include <QtTest>

#include "skin/skin.h"

using namespace linuxbee;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

SkinColour c(int r, int g, int b, int a = 255)
{
    SkinColour colour;
    colour.r = r;
    colour.g = g;
    colour.b = b;
    colour.a = a;
    return colour;
}

const QByteArray kSkin
    = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
      "<root>\r\n"
      "<variables>\r\n"
      "\tgray_3=\"43,43,43\"\r\n"
      "\tgray_3_icon=\"128,43,43,43\"\r\n"
      "</variables>\r\n"
      "<colours id=\"Default\">\r\n"
      "\t<element id=\"Icon.Mask\" bg=\"gray_3_icon\" fg=\"gray_3\" />\r\n"
      "\t<element id=\"Metro.Button\" bg=\"255,0,255,0\" bdr=\"0,10,20,30\" />\r\n"
      "\t<element id=\"StatusBar.Background\" bg=\" 64 , 1 , 2 , 3 \" />\r\n"
      "</colours>\r\n"
      "</root>\r\n";

}

class SkinsAlphaFirstTest : public QObject {
    Q_OBJECT
private slots:
    void readsAlphaFirst();
    void writesAlphaFirst();
    void theSheetKeepsItsOwnOrder();
};

void SkinsAlphaFirstTest::readsAlphaFirst()
{
    // SK-READ, "A colour": four numbers are alpha, red, green, blue (C-KRY); three are opaque.
    const Skin skin = readSkin(kSkin);
    QVERIFY2(skin.ok, qPrintable(skin.error));
    QCOMPARE(skin.problems, QStringList());
    QCOMPARE(skin.variables.value(s(u"gray_3")), c(43, 43, 43));
    QCOMPARE(skin.variables.value(s(u"gray_3_icon")), c(43, 43, 43, 128));
    const SkinElement icon = skin.elements.value(s(u"Icon.Mask"));
    QCOMPARE(icon.bg, std::optional<SkinColour>(c(43, 43, 43, 128))); // by a variable
    QCOMPARE(icon.fg, std::optional<SkinColour>(c(43, 43, 43)));
    const SkinElement metro = skin.elements.value(s(u"Metro.Button"));
    QCOMPARE(metro.bg, std::optional<SkinColour>(c(0, 255, 0, 255))); // opaque green
    QCOMPARE(metro.bdr, std::optional<SkinColour>(c(10, 20, 30, 0))); // wholly clear
    QCOMPARE(skin.elements.value(s(u"StatusBar.Background")).bg,
        std::optional<SkinColour>(c(1, 2, 3, 64))); // spaces allowed around each
}

void SkinsAlphaFirstTest::writesAlphaFirst()
{
    // SK-COLOURS' text is the skin's own: r,g,b, or a,r,g,b when the alpha is not 255. What
    // `linuxbeectl skin show` prints can be put back into a skin and reads the same.
    QCOMPARE(skinColourText(c(43, 43, 43, 128)), s(u"128,43,43,43"));
    QCOMPARE(skinColourText(c(1, 22, 255)), s(u"1,22,255"));
    QCOMPARE(skinColourText(c(10, 20, 30, 0)), s(u"0,10,20,30"));
    const QByteArray again
        = "<root><element id=\"A\" bg=\"" + skinColourText(c(9, 8, 7, 6)).toUtf8() + "\" /></root>";
    QCOMPARE(readSkin(again).elements.value(s(u"A")).bg, std::optional<SkinColour>(c(9, 8, 7, 6)));
}

void SkinsAlphaFirstTest::theSheetKeepsItsOwnOrder()
{
    // SK-SHEET: Qt's style sheets write rgba(r,g,b,a), alpha last, whatever the skin's order.
    const QMap<QString, SkinColour> colours
        = skinColours(readSkin("<root><element id=\"Panel.StatusBar.Default\" bg=\"128,43,43,43\" "
                               "fg=\"4,5,6\" /></root>"));
    QCOMPARE(colours.value(s(u"statusBar")), c(43, 43, 43, 128));
    QVERIFY(skinStyleSheet(colours).contains(s(u"rgba(43,43,43,128)")));
}

QTEST_MAIN(SkinsAlphaFirstTest)
#include "tst_skins_alpha_first.moc"
