// LOCKED ACCEPTANCE TEST (tag undo: a file that was not put back stays in the step). Written by
// the reviewer, not the builder. The builder makes these pass by changing src/, never by editing
// this file. A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are UN-KEEP in docs/spec/tag-undo.md. The fault this holds shut (the audit of
// undo, 2026-11-18, A4): an undo that could not write a file forgot the step all the same and
// deleted the covers kept for it. Nothing could be tried again, and a removed cover was gone.
//
// Every case works in a temporary folder, on copies of the fixtures.
#include <QSignalSpy>
#include <QtTest>

#include "tags/tagreader.h"
#include "tags/tagwriter.h"
#include "tagundo/tagundo.h"

using linuxbee::Picture;
using linuxbee::TagChanges;
using linuxbee::TagUndo;
using linuxbee::WriteResult;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

QString titleOf(const QString &path)
{
    return linuxbee::readTags(path).tags.first(QStringLiteral("Title"));
}

QList<Picture> picturesOf(const QString &path)
{
    linuxbee::ReadOptions options;
    options.pictures = true;
    options.pictureData = true;
    return linuxbee::readTags(path, options).tags.pictures;
}

TagChanges title(const QString &text)
{
    TagChanges c;
    c.fields.insert(QStringLiteral("Title"), { text });
    return c;
}

// The files in the scratch folder: the pictures a step keeps.
qsizetype keptFiles(const QString &scratch)
{
    return QDir(scratch).entryList(QDir::Files | QDir::Hidden).size();
}

// A writer that fails for the paths in `refuse` and saves the others.
TagUndo::Writer refusing(const QStringList &refuse)
{
    return [refuse](const QString &path, const TagChanges &changes) {
        if (refuse.contains(path)) {
            WriteResult failed;
            failed.status = WriteResult::Status::Failed;
            return failed;
        }
        return linuxbee::writeTags(path, changes);
    };
}

}

class TagUndoKeepTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void allPutBackIsForgotten();
    void aFileNotWrittenIsKept();
    void theOrderOfTheStepIsKept();
    void theCoverIsKept();
    void aFileThatIsGoneStaysKept();
    void aFileThatIsNotTheOneEditedIsKept();
    void aNewStepForgetsWhatWasKept();
    void whenTheObjectGoesTheKeptFilesGo();
    void anOpenChangeAndAKeptStep();
    void itSaysWhenItChanges();

private:
    QString copy(const char *fixture, const char *as);
    void edit(TagUndo &undo, const QStringList &paths);
    QString withCoverRemoved(TagUndo &undo, QByteArray *cover);
    QTemporaryDir m_dir;
    QString m_work;
    QString m_scratch;
    int m_counter = 0;
};

void TagUndoKeepTest::init()
{
    QVERIFY(m_dir.isValid());
    m_work = m_dir.filePath(QStringLiteral("t%1").arg(++m_counter));
    m_scratch = m_work + QStringLiteral("/scratch");
    QVERIFY(QDir().mkpath(m_work));
}

QString TagUndoKeepTest::copy(const char *fixture, const char *as)
{
    const QString target = m_work + QLatin1Char('/') + QLatin1String(as);
    QFile::remove(target);
    if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA "/tags/") + QLatin1String(fixture), target))
        QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
    QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner);
    return target;
}

// One step: the title of each file becomes "edited".
void TagUndoKeepTest::edit(TagUndo &undo, const QStringList &paths)
{
    undo.begin(s(u"Edit Tags"));
    for (const QString &path : paths) {
        if (undo.write(path, title(s(u"edited"))).status != WriteResult::Status::Ok)
            QTest::qFail("the edit was not saved", __FILE__, __LINE__);
    }
    undo.end();
}

// A file that is given a cover, and one step that removes it. `cover` is the cover's bytes.
QString TagUndoKeepTest::withCoverRemoved(TagUndo &undo, QByteArray *cover)
{
    const QString a = copy("basic.flac", "cover.flac");
    Picture picture;
    picture.type = QStringLiteral("Front Cover");
    picture.mimeType = QStringLiteral("image/png");
    picture.data = QByteArray("\x89PNG\r\n\x1a\n", 8) + QByteArray(300, 'c');
    *cover = picture.data;
    TagChanges give;
    give.pictures = QList<Picture> { picture };
    TagChanges take;
    take.pictures = QList<Picture> { };
    if (!linuxbee::writeTags(a, give).ok() || picturesOf(a).size() != 1)
        QTest::qFail("the file was not given its cover", __FILE__, __LINE__);
    undo.begin(s(u"Remove Tags"));
    if (undo.write(a, take).status != WriteResult::Status::Ok || !picturesOf(a).isEmpty())
        QTest::qFail("the cover was not removed", __FILE__, __LINE__);
    undo.end();
    return a;
}

void TagUndoKeepTest::allPutBackIsForgotten()
{
    // The control, and UN-STEP as it was: when every file was put back, nothing is left.
    TagUndo undo(m_scratch);
    QByteArray cover;
    const QString a = withCoverRemoved(undo, &cover);
    QCOMPARE(keptFiles(m_scratch), 1);
    QCOMPARE(undo.undo().restored, QStringList({ a }));
    QVERIFY(!undo.canUndo());
    QCOMPARE(undo.paths(), QStringList());
    QCOMPARE(undo.name(), QString());
    QCOMPARE(keptFiles(m_scratch), 0);
    const TagUndo::Outcome again = undo.undo();
    QVERIFY(again.restored.isEmpty() && again.failed.isEmpty() && again.changed.isEmpty());
}

void TagUndoKeepTest::aFileNotWrittenIsKept()
{
    // UN-KEEP: the file that could not be written stays in the step, and the next undo tries it.
    const QString a = copy("basic.flac", "a.flac");
    const QString b = copy("basic.mp3", "b.mp3");
    const QString c = copy("basic.ogg", "c.ogg");
    const QString titleOfB = titleOf(b);
    TagUndo undo(m_scratch);
    edit(undo, { a, b, c });
    const TagUndo::Outcome first = undo.undo(refusing({ b }));
    QCOMPARE(first.restored, QStringList({ a, c }));
    QCOMPARE(first.failed, QStringList({ b }));
    QVERIFY(undo.canUndo());
    QCOMPARE(undo.paths(), QStringList({ b }));
    QCOMPARE(undo.name(), s(u"Edit Tags"));
    QCOMPARE(titleOf(b), s(u"edited"));

    // The files that were put back are not written a second time.
    QVERIFY(linuxbee::writeTags(a, title(s(u"later"))).ok());
    const TagUndo::Outcome second = undo.undo();
    QCOMPARE(second.restored, QStringList({ b }));
    QCOMPARE(second.failed, QStringList());
    QCOMPARE(second.changed, QStringList());
    QCOMPARE(titleOf(b), titleOfB);
    QVERIFY(titleOfB != s(u"edited"));
    QCOMPARE(titleOf(a), s(u"later"));
    QVERIFY(!undo.canUndo());
}

void TagUndoKeepTest::theOrderOfTheStepIsKept()
{
    const QString a = copy("basic.flac", "a.flac");
    const QString b = copy("basic.mp3", "b.mp3");
    const QString c = copy("basic.ogg", "c.ogg");
    const QString d = copy("basic.m4a", "d.m4a");
    TagUndo undo(m_scratch);
    edit(undo, { a, b, c, d });
    const TagUndo::Outcome first = undo.undo(refusing({ d, b }));
    QCOMPARE(first.failed, QStringList({ b, d }));
    QCOMPARE(undo.paths(), QStringList({ b, d }));
    // Tried again and failed again: kept again.
    const TagUndo::Outcome second = undo.undo(refusing({ d }));
    QCOMPARE(second.restored, QStringList({ b }));
    QCOMPARE(second.failed, QStringList({ d }));
    QCOMPARE(undo.paths(), QStringList({ d }));
    QCOMPARE(undo.undo().restored, QStringList({ d }));
    QCOMPARE(titleOf(d), s(u"M4A Title"));
    QVERIFY(!undo.canUndo());
}

void TagUndoKeepTest::theCoverIsKept()
{
    // A4. The file is marked read-only when the undo comes (the writer refuses it: WS-REFUSED).
    // The cover that was removed is still kept, and comes back when the file can be written.
    TagUndo undo(m_scratch);
    QByteArray cover;
    const QString a = withCoverRemoved(undo, &cover);
    QVERIFY(QFile::setPermissions(a, QFile::ReadOwner));
    const TagUndo::Outcome first = undo.undo();
    QCOMPARE(first.failed, QStringList({ a }));
    QCOMPARE(first.restored, QStringList());
    QVERIFY(undo.canUndo());
    QCOMPARE(keptFiles(m_scratch), 1); // the cover's file is not deleted
    QVERIFY(picturesOf(a).isEmpty());

    QVERIFY(QFile::setPermissions(a, QFile::ReadOwner | QFile::WriteOwner));
    const TagUndo::Outcome second = undo.undo();
    QCOMPARE(second.restored, QStringList({ a }));
    const QList<Picture> back = picturesOf(a);
    QCOMPARE(back.size(), 1);
    QCOMPARE(back.at(0).data, cover);
    QVERIFY(!undo.canUndo());
    QCOMPARE(keptFiles(m_scratch), 0);
}

void TagUndoKeepTest::aFileThatIsGoneStaysKept()
{
    // Nothing at the path is a file that could not be written (UN-STEP). It is kept like any
    // other, for as long as the step is.
    const QString a = copy("basic.flac", "a.flac");
    TagUndo undo(m_scratch);
    edit(undo, { a });
    QVERIFY(QFile::rename(a, m_work + s(u"/away.flac")));
    QCOMPARE(undo.undo().failed, QStringList({ a }));
    QVERIFY(undo.canUndo());
    QCOMPARE(undo.undo().failed, QStringList({ a }));
    QVERIFY(!QFileInfo::exists(a)); // and undo made no file
    // The very file comes back to its path, and is put back.
    QVERIFY(QFile::rename(m_work + s(u"/away.flac"), a));
    QCOMPARE(undo.undo().restored, QStringList({ a }));
    QCOMPARE(titleOf(a), s(u"Flac Title"));
}

void TagUndoKeepTest::aFileThatIsNotTheOneEditedIsKept()
{
    // UN-SAME leaves a stranger at the path alone. What was kept for the file that was edited is
    // not thrown away for that: when the file is at its path again, undo puts it back.
    TagUndo undo(m_scratch);
    QByteArray cover;
    const QString a = withCoverRemoved(undo, &cover);
    QVERIFY(QFile::rename(a, m_work + s(u"/away.flac")));
    copy("love.flac", "cover.flac");
    const TagUndo::Outcome first = undo.undo();
    QCOMPARE(first.changed, QStringList({ a }));
    QCOMPARE(first.restored, QStringList());
    QVERIFY(undo.canUndo());
    QCOMPARE(undo.paths(), QStringList({ a }));
    QCOMPARE(keptFiles(m_scratch), 1);

    QVERIFY(QFile::remove(a));
    QVERIFY(QFile::rename(m_work + s(u"/away.flac"), a));
    QCOMPARE(undo.undo().restored, QStringList({ a }));
    QCOMPARE(picturesOf(a).size(), 1);
    QCOMPARE(picturesOf(a).at(0).data, cover);
}

void TagUndoKeepTest::aNewStepForgetsWhatWasKept()
{
    // UN-STEP, as it was: there is one step. The first file kept in a new one forgets the files
    // left over from the one before, and their pictures.
    TagUndo undo(m_scratch);
    QByteArray cover;
    const QString a = withCoverRemoved(undo, &cover);
    QCOMPARE(undo.undo(refusing({ a })).failed, QStringList({ a }));
    QCOMPARE(keptFiles(m_scratch), 1);
    const QString b = copy("basic.mp3", "b.mp3");
    edit(undo, { b });
    QCOMPARE(undo.paths(), QStringList({ b }));
    QCOMPARE(undo.name(), s(u"Edit Tags"));
    QCOMPARE(keptFiles(m_scratch), 0);
    QCOMPARE(undo.undo().restored, QStringList({ b }));
    QVERIFY(picturesOf(a).isEmpty());
}

void TagUndoKeepTest::whenTheObjectGoesTheKeptFilesGo()
{
    QByteArray cover;
    {
        TagUndo undo(m_scratch);
        const QString a = withCoverRemoved(undo, &cover);
        QCOMPARE(undo.undo(refusing({ a })).failed, QStringList({ a }));
        QCOMPARE(keptFiles(m_scratch), 1);
    }
    QCOMPARE(keptFiles(m_scratch), 0);
}

void TagUndoKeepTest::anOpenChangeAndAKeptStep()
{
    // UN-STEP, as it was, for a step with files left over: not while a change is open, and a
    // change that keeps nothing leaves it.
    const QString a = copy("basic.flac", "a.flac");
    TagUndo undo(m_scratch);
    edit(undo, { a });
    QCOMPARE(undo.undo(refusing({ a })).failed, QStringList({ a }));
    undo.begin(s(u"Capitalise"));
    QVERIFY(!undo.canUndo());
    undo.end();
    QVERIFY(undo.canUndo());
    QCOMPARE(undo.name(), s(u"Edit Tags"));
    QCOMPARE(undo.undo().restored, QStringList({ a }));
    QCOMPARE(titleOf(a), s(u"Flac Title"));
}

void TagUndoKeepTest::itSaysWhenItChanges()
{
    const QString a = copy("basic.flac", "a.flac");
    const QString b = copy("basic.mp3", "b.mp3");
    TagUndo undo(m_scratch);
    edit(undo, { a, b });
    QSignalSpy spy(&undo, &TagUndo::changed);
    undo.undo(refusing({ b })); // paths() answers differently now
    QCOMPARE(spy.count(), 1);
    undo.undo(); // and canUndo() now
    QCOMPARE(spy.count(), 2);
}

QTEST_GUILESS_MAIN(TagUndoKeepTest)
#include "tst_tagundo_keep.moc"
