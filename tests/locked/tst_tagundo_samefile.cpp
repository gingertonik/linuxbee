// LOCKED ACCEPTANCE TEST (tag undo: only into the file that was edited). Written by the reviewer,
// not the builder. The builder makes these pass by changing src/, never by editing this file. A
// test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are UN-SAME and UN-MOVED in docs/spec/tag-undo.md. The fault this holds shut
// (the write-path audit, part 2, F2): undo wrote the earlier tags into whatever file had the
// path, and said "put back".
//
// Every case works in a temporary folder, on copies of the fixtures.
#include <QtTest>

#include "tags/tagreader.h"
#include "tags/tagwriter.h"
#include "tagundo/tagundo.h"

using linuxbee::TagChanges;
using linuxbee::TagUndo;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

QByteArray bytesOf(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArrayLiteral("<cannot open>");
    return file.readAll();
}

QString titleOf(const QString &path)
{
    return linuxbee::readTags(path).tags.first(QStringLiteral("Title"));
}

TagChanges title(const QString &text)
{
    TagChanges c;
    c.fields.insert(QStringLiteral("Title"), { text });
    return c;
}

}

class TagUndoSameFileTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void theFileThatWasEdited();
    void anotherFileAtThePath();
    void theSameFileChangedSince();
    void aFileThatIsGoneIsFailedAsBefore();
    void aMoveIsFollowed();
    void aMoveThatWasACopy();
    void aMoveAndAStrangerAtTheOldPath();
    void aMoveOfAFileNotInTheStep();
    void writtenTwiceInOneStep();
    void someOfSeveral();

private:
    QString copy(const char *fixture, const char *as);
    void edit(TagUndo &undo, const QStringList &paths);
    QTemporaryDir m_dir;
    QString m_work;
    int m_counter = 0;
};

void TagUndoSameFileTest::init()
{
    QVERIFY(m_dir.isValid());
    m_work = m_dir.filePath(QStringLiteral("t%1").arg(++m_counter));
    QVERIFY(QDir().mkpath(m_work));
}

QString TagUndoSameFileTest::copy(const char *fixture, const char *as)
{
    const QString target = m_work + QLatin1Char('/') + QLatin1String(as);
    QFile::remove(target);
    if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/") + QLatin1String(fixture), target))
        QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
    QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner);
    return target;
}

// One step: the title of each file becomes "edited".
void TagUndoSameFileTest::edit(TagUndo &undo, const QStringList &paths)
{
    undo.begin(s(u"Edit Tags"));
    for (const QString &path : paths) {
        if (!undo.write(path, title(s(u"edited"))).ok())
            QTest::qFail("the edit was not saved", __FILE__, __LINE__);
    }
    undo.end();
}

void TagUndoSameFileTest::theFileThatWasEdited()
{
    // The control: nothing happened to the file between the edit and the undo.
    const QString a = copy("basic.flac", "a.flac");
    TagUndo undo(m_work + s(u"/scratch"));
    edit(undo, { a });
    const TagUndo::Outcome out = undo.undo();
    QCOMPARE(out.restored, QStringList({ a }));
    QCOMPARE(out.changed, QStringList());
    QCOMPARE(out.failed, QStringList());
    QCOMPARE(titleOf(a), s(u"Flac Title"));
}

void TagUndoSameFileTest::anotherFileAtThePath()
{
    // UN-SAME: the file that was edited is gone from its path, and another file has the path.
    // Nothing is written into it. It is named as changed, not as put back.
    const QString a = copy("basic.flac", "a.flac");
    TagUndo undo(m_work + s(u"/scratch"));
    edit(undo, { a });
    QVERIFY(QFile::rename(a, m_work + s(u"/elsewhere.flac")));
    copy("love.flac", "a.flac");
    const QByteArray stranger = bytesOf(a);
    const TagUndo::Outcome out = undo.undo();
    QCOMPARE(out.restored, QStringList());
    QCOMPARE(out.changed, QStringList({ a }));
    QCOMPARE(out.failed, QStringList());
    QCOMPARE(bytesOf(a), stranger); // BYTES: not a byte of it
    QCOMPARE(titleOf(m_work + s(u"/elsewhere.flac")), s(u"edited")); // nobody said where it went
}

void TagUndoSameFileTest::theSameFileChangedSince()
{
    // UN-SAME: the file was saved again, by something that is not this step. Undo no longer knows
    // what it would be writing over, and leaves it.
    const QString a = copy("basic.flac", "a.flac");
    TagUndo undo(m_work + s(u"/scratch"));
    edit(undo, { a });
    QVERIFY(linuxbee::writeTags(a, title(s(u"edited again, elsewhere"))).ok());
    const QByteArray now = bytesOf(a);
    const TagUndo::Outcome out = undo.undo();
    QCOMPARE(out.restored, QStringList());
    QCOMPARE(out.changed, QStringList({ a }));
    QCOMPARE(bytesOf(a), now);
}

void TagUndoSameFileTest::aFileThatIsGoneIsFailedAsBefore()
{
    // UN-STEP, unchanged: nothing at the path is a file that could not be written.
    const QString a = copy("basic.flac", "a.flac");
    TagUndo undo(m_work + s(u"/scratch"));
    edit(undo, { a });
    QVERIFY(QFile::remove(a));
    const TagUndo::Outcome out = undo.undo();
    QCOMPARE(out.failed, QStringList({ a }));
    QCOMPARE(out.changed, QStringList());
    QVERIFY(!QFileInfo::exists(a)); // and undo made no file
}

void TagUndoSameFileTest::aMoveIsFollowed()
{
    // UN-MOVED: told that the file has another path, the step has that path.
    const QString a = copy("basic.flac", "a.flac");
    const QString to = m_work + s(u"/sub/renamed.flac");
    TagUndo undo(m_work + s(u"/scratch"));
    edit(undo, { a });
    QVERIFY(QDir().mkpath(m_work + s(u"/sub")));
    QVERIFY(QFile::rename(a, to));
    QSignalSpy changed(&undo, &TagUndo::changed);
    undo.moved({ { a, to } });
    QCOMPARE(int(changed.size()), 1); // paths() answers differently now
    QCOMPARE(undo.paths(), QStringList({ to }));
    const TagUndo::Outcome out = undo.undo();
    QCOMPARE(out.restored, QStringList({ to }));
    QCOMPARE(out.changed, QStringList());
    QCOMPARE(titleOf(to), s(u"Flac Title"));
    QVERIFY(!QFileInfo::exists(a));
}

void TagUndoSameFileTest::aMoveThatWasACopy()
{
    // UN-MOVED: a move to another disk is a copy and a removal, and the copy is another file to
    // the system. The one who says it moved is believed when the file at the new path has the
    // size the edited file had; with another size it is not the file.
    const QString a = copy("basic.flac", "a.flac");
    const QString b = copy("basic.mp3", "b.mp3");
    const QString toA = m_work + s(u"/a2.flac");
    const QString toB = m_work + s(u"/b2.mp3");
    TagUndo undo(m_work + s(u"/scratch"));
    edit(undo, { a, b });
    QVERIFY(QFile::copy(a, toA));
    QVERIFY(QFile::remove(a));
    QVERIFY(QFile::remove(b));
    copy("v1only.mp3", "b2.mp3"); // not b: another size
    QVERIFY(QFileInfo(toB).size() != 0);
    const QByteArray stranger = bytesOf(toB);
    undo.moved({ { a, toA }, { b, toB } });
    const TagUndo::Outcome out = undo.undo();
    QCOMPARE(out.restored, QStringList({ toA }));
    QCOMPARE(out.changed, QStringList({ toB }));
    QCOMPARE(titleOf(toA), s(u"Flac Title"));
    QCOMPARE(bytesOf(toB), stranger);
}

void TagUndoSameFileTest::aMoveAndAStrangerAtTheOldPath()
{
    // The audit's case whole: the edited file is renamed, the step is told, and then another
    // file is given the old name. Undo puts the tags back into the edited file, where it is now,
    // and the file with the old name is not touched.
    const QString a = copy("basic.flac", "a.flac");
    const QString to = m_work + s(u"/edited.flac");
    TagUndo undo(m_work + s(u"/scratch"));
    edit(undo, { a });
    QVERIFY(QFile::rename(a, to));
    undo.moved({ { a, to } });
    copy("love.flac", "a.flac");
    const QByteArray stranger = bytesOf(a);
    const TagUndo::Outcome out = undo.undo();
    QCOMPARE(out.restored, QStringList({ to }));
    QCOMPARE(out.changed, QStringList());
    QCOMPARE(titleOf(to), s(u"Flac Title"));
    QCOMPARE(bytesOf(a), stranger);
}

void TagUndoSameFileTest::aMoveOfAFileNotInTheStep()
{
    // UN-MOVED: a move of some other file changes nothing and is not announced.
    const QString a = copy("basic.flac", "a.flac");
    TagUndo undo(m_work + s(u"/scratch"));
    edit(undo, { a });
    QSignalSpy changed(&undo, &TagUndo::changed);
    undo.moved({ { m_work + s(u"/x.flac"), m_work + s(u"/y.flac") } });
    QCOMPARE(int(changed.size()), 0);
    QCOMPARE(undo.paths(), QStringList({ a }));
    QCOMPARE(undo.undo().restored, QStringList({ a }));
}

void TagUndoSameFileTest::writtenTwiceInOneStep()
{
    // UN-SAME: the file is known as it was after the last write of the step.
    const QString a = copy("basic.flac", "a.flac");
    TagUndo undo(m_work + s(u"/scratch"));
    undo.begin(s(u"Edit Tags"));
    QVERIFY(undo.write(a, title(s(u"once"))).ok());
    TagChanges album;
    album.fields.insert(s(u"Album"), { s(u"another album") });
    QVERIFY(undo.write(a, album).ok());
    undo.end();
    const TagUndo::Outcome out = undo.undo();
    QCOMPARE(out.restored, QStringList({ a }));
    QCOMPARE(titleOf(a), s(u"Flac Title"));
    QCOMPARE(linuxbee::readTags(a).tags.first(s(u"Album")), s(u"Flac Album"));
}

void TagUndoSameFileTest::someOfSeveral()
{
    // Each file is judged by itself, and the lists keep the step's order.
    const QString a = copy("basic.flac", "a.flac");
    const QString b = copy("basic.mp3", "b.mp3");
    const QString c = copy("basic.ogg", "c.ogg");
    const QString d = copy("basic.m4a", "d.m4a");
    TagUndo undo(m_work + s(u"/scratch"));
    edit(undo, { a, b, c, d });
    QVERIFY(QFile::remove(b));
    copy("v1only.mp3", "b.mp3"); // another file at b
    QVERIFY(QFile::remove(c)); // c is gone
    const TagUndo::Outcome out = undo.undo();
    QCOMPARE(out.restored, QStringList({ a, d }));
    QCOMPARE(out.changed, QStringList({ b }));
    QCOMPARE(out.failed, QStringList({ c }));
    QCOMPARE(titleOf(a), s(u"Flac Title"));
    QCOMPARE(titleOf(d), s(u"M4A Title"));
}

QTEST_GUILESS_MAIN(TagUndoSameFileTest)
#include "tst_tagundo_samefile.moc"
