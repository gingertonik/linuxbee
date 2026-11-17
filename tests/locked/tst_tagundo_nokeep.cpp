// LOCKED ACCEPTANCE TEST (tag undo: made with no folder, it keeps no picture anywhere). Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/tag-undo.md (UN-NOKEEP).
#include <QTemporaryDir>
#include <QtTest>

#include "tags/tagreader.h"
#include "tags/tagwriter.h"
#include "tagundo/tagundo.h"

using namespace linuxbee;

class TagUndoNoKeepTest : public QObject {
    Q_OBJECT
private slots:
    void noFolderNoPicture();
};

void TagUndoNoKeepTest::noFolderNoPicture()
{
    // UN-NOKEEP: an empty folder name is not "the current folder". It means: keep no picture.
    QTemporaryDir here;
    const QString was = QDir::currentPath();
    QVERIFY(QDir::setCurrent(here.path()));
    const auto back = qScopeGuard([was] { QDir::setCurrent(was); });
    const QString path = here.path() + QStringLiteral("/b.m4a");
    QVERIFY(QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/basic.m4a"), path)); // GEN: one picture
    QCOMPARE(readTags(path).tags.pictures.size(), 1);
    const QStringList only { QStringLiteral("b.m4a") };

    TagUndo undo { QString() };
    TagChanges noPictures;
    noPictures.pictures = QList<Picture> { };
    undo.begin(QStringLiteral("Test"));
    const WriteResult refused = undo.write(path, noPictures);
    undo.end();
    QVERIFY(!refused.ok()); // UN-RECORD: a picture that cannot be kept stops the write
    QCOMPARE(readTags(path).tags.pictures.size(), 1);
    QVERIFY(!undo.canUndo());
    QCOMPARE(QDir(here.path()).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot), only);

    // Everything else is as usual.
    const QString before = readTags(path).tags.first(QStringLiteral("Album"));
    TagChanges album;
    album.fields = { { QStringLiteral("Album"), { QStringLiteral("Changed") } } };
    undo.begin(QStringLiteral("Test"));
    QVERIFY(undo.write(path, album).ok());
    undo.end();
    QVERIFY(undo.canUndo());
    QVERIFY(undo.undo().failed.isEmpty());
    QCOMPARE(readTags(path).tags.first(QStringLiteral("Album")), before);
    QCOMPARE(QDir(here.path()).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot), only);
}

QTEST_MAIN(TagUndoNoKeepTest)
#include "tst_tagundo_nokeep.moc"
