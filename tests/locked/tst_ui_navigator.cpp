// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/navigator.md (V-x). Runs on the
// offscreen platform.
#include <QSignalSpy>
#include <QTreeWidget>
#include <QtTest>

#include "navigator/navigatorpanel.h"

using linuxbee::ui::NavigatorPanel;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

// The tree as text: one line per row, two spaces of indent per level.
QStringList outline(const QTreeWidget *tree)
{
    QStringList out;
    if (!tree)
        return { QStringLiteral("NO TREE") };
    for (QTreeWidgetItemIterator it(const_cast<QTreeWidget *>(tree)); *it; ++it) {
        int depth = 0;
        for (QTreeWidgetItem *p = (*it)->parent(); p; p = p->parent())
            ++depth;
        out.append(QString(depth * 2, QLatin1Char(' ')) + (*it)->text(0));
    }
    return out;
}

QTreeWidgetItem *row(QTreeWidget *tree, const QString &text)
{
    for (QTreeWidgetItemIterator it(tree); *it; ++it)
        if ((*it)->text(0) == text)
            return *it;
    return nullptr;
}

QTreeWidget *treeOf(NavigatorPanel &p)
{
    return p.findChild<QTreeWidget *>(s(u"navigatorTree"));
}

QStringList heard(QSignalSpy &spy)
{
    QStringList out;
    for (const auto &e : spy)
        out.append(e.at(0).toString());
    spy.clear();
    return out;
}

}

class NavigatorTest : public QObject {
    Q_OBJECT
private slots:
    void structure();
    void counts();
    void playlists();
    void playlistNamesCleaned();
    void selecting();
    void clicking();
    void headingsAreNotChoices();
    void selectionSurvivesNewPlaylists();
    void selectionLost();
};

void NavigatorTest::structure()
{
    // V-FRAME, V-LIBRARY
    NavigatorPanel p;
    QTreeWidget *tree = treeOf(p);
    QVERIFY(tree);
    QVERIFY(tree->isHeaderHidden());
    QVERIFY(tree->selectionMode() == QAbstractItemView::SingleSelection);
    QVERIFY(tree->editTriggers() == QAbstractItemView::NoEditTriggers);
    QCOMPARE(outline(tree),
        QStringList({ s(u"Playing Tracks"), s(u"Library"), s(u"  Music"), s(u"  Inbox"), s(u"  Audiobooks"), s(u"  Podcasts"),
            s(u"  Videos"), s(u"Playlists") }));
    QCOMPARE(p.nodes(),
        QStringList({ s(u"playing"), s(u"library:Music"), s(u"library:Inbox"), s(u"library:Audiobooks"), s(u"library:Podcasts"),
            s(u"library:Videos") }));
    // It opens on the music library, with the groups open.
    QCOMPARE(p.currentNode(), s(u"library:Music"));
    QVERIFY(tree->currentItem());
    QCOMPARE(tree->currentItem()->text(0), s(u"Music"));
    QVERIFY(row(tree, s(u"Library"))->isExpanded());
    p.resize(240, 500);
    p.show();
    QVERIFY(QTest::qWaitForWindowExposed(&p));
    QCOMPARE(tree->size(), p.size()); // the tree fills the panel
}

void NavigatorTest::counts()
{
    // V-LIBRARY
    NavigatorPanel p;
    QTreeWidget *tree = treeOf(p);
    QVERIFY(tree);
    QSignalSpy selected(&p, &NavigatorPanel::nodeSelected);
    p.setLibraryCounts({ { s(u"Music"), 12345 }, { s(u"Inbox"), 7 }, { s(u"Podcasts"), 0 }, { s(u"Nonsense"), 3 } });
    QCOMPARE(outline(tree).mid(1, 6),
        QStringList({ s(u"Library"), s(u"  Music (12,345)"), s(u"  Inbox (7)"), s(u"  Audiobooks"), s(u"  Podcasts"), s(u"  Videos") }));
    p.setLibraryCounts({ { s(u"Music"), 1000 }, { s(u"Videos"), -4 } }); // a section not named is 0 again
    QCOMPARE(outline(tree).mid(2, 5),
        QStringList({ s(u"  Music (1,000)"), s(u"  Inbox"), s(u"  Audiobooks"), s(u"  Podcasts"), s(u"  Videos") }));
    // Counts change the text, never the ids or the choice.
    QCOMPARE(p.currentNode(), s(u"library:Music"));
    QCOMPARE(p.nodes().size(), 6);
    QCOMPARE(selected.size(), 0);
    QVERIFY(p.selectNode(s(u"library:Inbox")));
}

void NavigatorTest::playlists()
{
    // V-PLAYLISTS
    NavigatorPanel p;
    QTreeWidget *tree = treeOf(p);
    QVERIFY(tree);
    p.setPlaylists({ s(u"Morning"), s(u"Rock/Best of"), s(u"Rock/Live/1999"), s(u"Rock/Anthems"), s(u"Zzz"), s(u"Jazz/Late") });
    // In the order given; a folder appears where its first playlist does.
    QCOMPARE(outline(tree).mid(7),
        QStringList({ s(u"Playlists"), s(u"  Morning"), s(u"  Rock"), s(u"    Best of"), s(u"    Live"), s(u"      1999"),
            s(u"    Anthems"), s(u"  Zzz"), s(u"  Jazz"), s(u"    Late") }));
    QCOMPARE(p.nodes().mid(6),
        QStringList({ s(u"playlist:Morning"), s(u"playlist:Rock/Best of"), s(u"playlist:Rock/Live/1999"),
            s(u"playlist:Rock/Anthems"), s(u"playlist:Zzz"), s(u"playlist:Jazz/Late") }));
    QVERIFY(row(tree, s(u"Rock"))->isExpanded());
    QVERIFY(row(tree, s(u"Live"))->isExpanded());
    // A new list replaces the old one entirely.
    p.setPlaylists({ s(u"Only") });
    QCOMPARE(outline(tree).mid(7), QStringList({ s(u"Playlists"), s(u"  Only") }));
    QCOMPARE(p.nodes().mid(6), QStringList({ s(u"playlist:Only") }));
    p.setPlaylists({ });
    QCOMPARE(outline(tree).mid(7), QStringList({ s(u"Playlists") }));
    QCOMPARE(p.nodes().size(), 6);
    // A playlist and a folder may share a name.
    p.setPlaylists({ s(u"Rock"), s(u"Rock/Best of") });
    QCOMPARE(outline(tree).mid(7), QStringList({ s(u"Playlists"), s(u"  Rock"), s(u"  Rock"), s(u"    Best of") }));
    QVERIFY(p.selectNode(s(u"playlist:Rock")));
    QVERIFY(p.selectNode(s(u"playlist:Rock/Best of")));
}

void NavigatorTest::playlistNamesCleaned()
{
    // V-PLAYLISTS: names are taken as the store gives them, less what cannot be a name.
    NavigatorPanel p;
    p.setPlaylists({ s(u" Morning "), s(u"/Rock//Best of/"), s(u""), s(u" / "), s(u"Morning"), s(u"Rock/Best of"), s(u"morning") });
    QCOMPARE(p.nodes().mid(6), QStringList({ s(u"playlist:Morning"), s(u"playlist:Rock/Best of"), s(u"playlist:morning") }));
}

void NavigatorTest::selecting()
{
    // V-NODES
    NavigatorPanel p;
    p.setPlaylists({ s(u"Rock/Best of"), s(u"Morning") });
    QSignalSpy selected(&p, &NavigatorPanel::nodeSelected);
    QSignalSpy activated(&p, &NavigatorPanel::nodeActivated);
    QVERIFY(p.selectNode(s(u"playlist:Rock/Best of")));
    QCOMPARE(p.currentNode(), s(u"playlist:Rock/Best of"));
    QCOMPARE(treeOf(p)->currentItem()->text(0), s(u"Best of"));
    QVERIFY(treeOf(p)->currentItem()->isSelected());
    QCOMPARE(heard(selected), QStringList({ s(u"playlist:Rock/Best of") }));
    QVERIFY(p.selectNode(s(u"playlist:Rock/Best of"))); // already chosen: fine, and nothing to say
    QCOMPARE(selected.size(), 0);
    QVERIFY(p.selectNode(s(u"playing")));
    QVERIFY(p.selectNode(s(u"library:Videos")));
    QCOMPARE(heard(selected), QStringList({ s(u"playing"), s(u"library:Videos") }));
    // Refused, leaving the choice: unknown ids, folders, headings, another spelling.
    for (const char16_t *bad : { u"library:Nonsense", u"playlist:Rock", u"playlist:Nope", u"Library", u"", u"library:music", u"PLAYING" })
        QVERIFY2(!p.selectNode(s(bad)), qPrintable(s(bad)));
    QCOMPARE(p.currentNode(), s(u"library:Videos"));
    QCOMPARE(selected.size(), 0);
    QCOMPARE(activated.size(), 0); // choosing is not activating
}

void NavigatorTest::clicking()
{
    NavigatorPanel p;
    p.setPlaylists({ s(u"Rock/Best of"), s(u"Morning") });
    p.resize(260, 500);
    p.show();
    QVERIFY(QTest::qWaitForWindowExposed(&p));
    QTreeWidget *tree = treeOf(p);
    QVERIFY(tree);
    QSignalSpy selected(&p, &NavigatorPanel::nodeSelected);
    QSignalSpy activated(&p, &NavigatorPanel::nodeActivated);
    auto centre = [tree](const char16_t *text) { return tree->visualItemRect(row(tree, s(text))).center(); };
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, { }, centre(u"Inbox"));
    QCOMPARE(heard(selected), QStringList({ s(u"library:Inbox") }));
    QCOMPARE(p.currentNode(), s(u"library:Inbox"));
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, { }, centre(u"Morning"));
    QCOMPARE(heard(selected), QStringList({ s(u"playlist:Morning") }));
    QCOMPARE(activated.size(), 0);
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, { }, centre(u"Best of"));
    QCOMPARE(activated.size(), 0);
    QTest::mouseDClick(tree->viewport(), Qt::LeftButton, { }, centre(u"Best of"));
    QCOMPARE(heard(activated), QStringList({ s(u"playlist:Rock/Best of") }));
    QCOMPARE(p.currentNode(), s(u"playlist:Rock/Best of"));
    // The keyboard moves the choice too, and Return activates.
    tree->setFocus();
    QTest::keyClick(tree, Qt::Key_Down);
    QCOMPARE(p.currentNode(), s(u"playlist:Morning"));
    QTest::keyClick(tree, Qt::Key_Return);
    QCOMPARE(heard(activated), QStringList({ s(u"playlist:Morning") }));
}

void NavigatorTest::headingsAreNotChoices()
{
    // V-NODES: a heading or a folder can be opened and closed but not chosen.
    NavigatorPanel p;
    p.setPlaylists({ s(u"Rock/Best of") });
    p.resize(260, 500);
    p.show();
    QVERIFY(QTest::qWaitForWindowExposed(&p));
    QTreeWidget *tree = treeOf(p);
    QVERIFY(tree);
    QSignalSpy selected(&p, &NavigatorPanel::nodeSelected);
    QSignalSpy activated(&p, &NavigatorPanel::nodeActivated);
    for (const char16_t *heading : { u"Library", u"Playlists", u"Rock" }) {
        QTreeWidgetItem *item = row(tree, s(heading));
        QVERIFY(item);
        QVERIFY2(!(item->flags() & Qt::ItemIsSelectable), qPrintable(s(heading)));
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, { }, tree->visualItemRect(item).center());
        QTest::mouseDClick(tree->viewport(), Qt::LeftButton, { }, tree->visualItemRect(item).center());
        QCOMPARE(p.currentNode(), s(u"library:Music"));
        QVERIFY(!item->isSelected());
    }
    QCOMPARE(selected.size(), 0);
    QCOMPARE(activated.size(), 0);
}

void NavigatorTest::selectionSurvivesNewPlaylists()
{
    // V-PLAYLISTS: the list is replaced often (every save); the choice stays when it still exists.
    NavigatorPanel p;
    p.setPlaylists({ s(u"Morning"), s(u"Rock/Best of") });
    QVERIFY(p.selectNode(s(u"playlist:Rock/Best of")));
    QSignalSpy selected(&p, &NavigatorPanel::nodeSelected);
    p.setPlaylists({ s(u"Aaa"), s(u"Rock/Best of"), s(u"Morning") });
    QCOMPARE(p.currentNode(), s(u"playlist:Rock/Best of"));
    QCOMPARE(treeOf(p)->currentItem()->text(0), s(u"Best of"));
    QVERIFY(treeOf(p)->currentItem()->isSelected());
    QCOMPARE(selected.size(), 0); // nothing changed for the listener
    // A library choice is untouched by playlist changes.
    QVERIFY(p.selectNode(s(u"library:Inbox")));
    selected.clear();
    p.setPlaylists({ });
    QCOMPARE(p.currentNode(), s(u"library:Inbox"));
    QCOMPARE(selected.size(), 0);
}

void NavigatorTest::selectionLost()
{
    // V-PLAYLISTS: the chosen playlist is gone: back to the music library, and that is announced.
    NavigatorPanel p;
    p.setPlaylists({ s(u"Morning"), s(u"Rock/Best of") });
    QVERIFY(p.selectNode(s(u"playlist:Morning")));
    QSignalSpy selected(&p, &NavigatorPanel::nodeSelected);
    p.setPlaylists({ s(u"Rock/Best of") });
    QCOMPARE(p.currentNode(), s(u"library:Music"));
    QCOMPARE(heard(selected), QStringList({ s(u"library:Music") }));
    QCOMPARE(treeOf(p)->currentItem()->text(0), s(u"Music"));
}

QTEST_MAIN(NavigatorTest)
#include "tst_ui_navigator.moc"
