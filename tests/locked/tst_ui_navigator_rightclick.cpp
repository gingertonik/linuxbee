// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// V-MENU (docs/spec/navigator.md): a right click does not change what is
// chosen. The clicks go through the window, as a display delivers them, so
// a double right-click arrives as a press and then a double-click event.
// Runs on the offscreen platform.
#include <QSignalSpy>
#include <QTreeWidget>
#include <QtTest>

#include "navigator/navigatorpanel.h"

using namespace linuxbee::ui;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

struct Shown {
    NavigatorPanel nav;
    QTreeWidget *tree = nullptr;

    Shown()
    {
        nav.resize(300, 600);
        nav.setPlaylists({ s(u"Morning"), s(u"Rock/Best of") });
        nav.show();
        tree = nav.findChild<QTreeWidget *>(s(u"navigatorTree"));
    }
    // The middle of the row with this text, in the window's coordinates.
    QPoint at(const QString &text) const
    {
        for (QTreeWidgetItemIterator it(tree); *it; ++it) {
            if ((*it)->text(0) == text)
                return tree->viewport()->mapTo(&nav, tree->visualItemRect(*it).center());
        }
        return { };
    }
};

}

class NavigatorRightClickTest : public QObject {
    Q_OBJECT
private slots:
    void rightDoubleClickChoosesNothing();
    void leftDoubleClickStillActivates();
};

void NavigatorRightClickTest::rightDoubleClickChoosesNothing()
{
    Shown w;
    QVERIFY(w.tree);
    QVERIFY(QTest::qWaitForWindowExposed(&w.nav));
    QSignalSpy chosen(&w.nav, &NavigatorPanel::nodeSelected);
    QSignalSpy activated(&w.nav, &NavigatorPanel::nodeActivated);
    QSignalSpy menus(&w.nav, &NavigatorPanel::menuRequested);
    for (const char16_t *text : { u"Inbox", u"Morning", u"Best of", u"Playing Tracks" }) {
        const QPoint p = w.at(s(text));
        QVERIFY2(!p.isNull(), qPrintable(s(text)));
        QTest::mouseDClick(w.nav.windowHandle(), Qt::RightButton, { }, p);
        QVERIFY2(w.nav.currentNode() == s(u"library:Music"), qPrintable(s(text) + s(u" -> ") + w.nav.currentNode()));
    }
    QCOMPARE(chosen.size(), 0);
    QCOMPARE(activated.size(), 0);
    QVERIFY(menus.size() >= 4); // the menu is still asked for on every row
}

void NavigatorRightClickTest::leftDoubleClickStillActivates()
{
    Shown w;
    QVERIFY(w.tree);
    QVERIFY(QTest::qWaitForWindowExposed(&w.nav));
    QSignalSpy activated(&w.nav, &NavigatorPanel::nodeActivated);
    QTest::mouseDClick(w.nav.windowHandle(), Qt::LeftButton, { }, w.at(s(u"Morning")));
    QCOMPARE(w.nav.currentNode(), s(u"playlist:Morning"));
    QCOMPARE(activated.size(), 1);
    QCOMPARE(activated.at(0).at(0).toString(), s(u"playlist:Morning"));
}

QTEST_MAIN(NavigatorRightClickTest)
#include "tst_ui_navigator_rightclick.moc"
