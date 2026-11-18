// LOCKED ACCEPTANCE TEST (one step of undo for tag changes). Written by the reviewer, not the
// builder. The builder makes these pass by changing src/, never by editing this file. A test
// believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/tag-undo.md (UN-RECORD, UN-STEP).
// Works on copies of the fixtures in a temporary folder, with the real
// reader and writer. What a file holds is judged by readTags.
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "tags/tagreader.h"
#include "tagundo/tagundo.h"

using namespace linuxbee;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

QByteArray png(const char *extra = "")
{
    return QByteArray::fromHex(
               "89504e470d0a1a0a0000000d4948445200000001000000010802000000907753de"
               "0000000c49444154789c63f8cfc0000003010100c9fe92ef0000000049454e44ae426082")
        + extra;
}

Picture cover(const char *extra = "")
{
    Picture p;
    p.data = png(extra);
    p.type = s(u"Front Cover");
    p.mimeType = s(u"image/png");
    return p;
}

TagChanges fields(std::initializer_list<std::pair<QString, QStringList>> list)
{
    TagChanges c;
    for (const auto &entry : list)
        c.fields.insert(entry.first, entry.second);
    return c;
}

using Status = WriteResult::Status;

}

class TagUndoTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void aChangeAndBack_data();
    void aChangeAndBack();
    void onlyWhatWasNamed();
    void oneStep();
    void picturesComeBack();
    void twiceInOneStep();
    void whatIsNotKept();
    void whenItCannotBeKept();
    void undoThatFails();

private:
    QString copy(const char *fixture, const char *as = nullptr)
    {
        const QString target = m_dir->filePath(QString::fromLatin1(as ? as : fixture));
        if (!QFile::copy(QStringLiteral(LINUXBEE_TEST_DATA) + QStringLiteral("/tags/")
                    + QString::fromLatin1(fixture),
                target))
            QTest::qFail("could not copy a fixture", __FILE__, __LINE__);
        return target;
    }
    static TrackTags tagsOf(const QString &path) { return readTags(path).tags; }
    QStringList kept() const
    {
        return QDir(m_scratch).entryList(QDir::Files | QDir::Hidden | QDir::System);
    }
    std::unique_ptr<QTemporaryDir> m_dir;
    QString m_scratch;
};

void TagUndoTest::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_scratch = m_dir->filePath(QStringLiteral("undo-scratch"));
}

void TagUndoTest::aChangeAndBack_data()
{
    QTest::addColumn<QString>("fixture");
    for (const char *name :
        { "basic.mp3", "basic.flac", "basic.ogg", "basic.m4a", "basic.opus", "empty.flac" })
        QTest::newRow(name) << QString::fromLatin1(name);
}

void TagUndoTest::aChangeAndBack()
{
    // UN-RECORD, UN-STEP: a change, and undo puts the earlier values back.
    QFETCH(QString, fixture);
    const QString path = copy(qPrintable(fixture));
    const TrackTags before = tagsOf(path);
    TagUndo undo(m_scratch);
    QSignalSpy changed(&undo, &TagUndo::changed);
    QVERIFY(!undo.canUndo());
    QCOMPARE(undo.name(), QString());

    undo.begin(s(u"Edit Tags"));
    const TagChanges change = fields(
        { { s(u"Title"), { s(u"A New Title") } }, { s(u"Artist"), { s(u"One"), s(u"Two") } },
            { s(u"Genre"), { } }, { s(u"Comment"), { s(u"written by the test") } } });
    QVERIFY(undo.write(path, change).status == Status::Ok);
    QVERIFY(!undo.canUndo()); // not while the change is being made
    QCOMPARE(changed.count(), 0);
    undo.end();
    QCOMPARE(changed.count(), 1);
    QVERIFY(undo.canUndo());
    QCOMPARE(undo.name(), s(u"Edit Tags"));
    QCOMPARE(undo.paths(), QStringList({ path }));
    QCOMPARE(tagsOf(path).fields.value(s(u"Title")), QStringList({ s(u"A New Title") }));
    QVERIFY(!tagsOf(path).fields.contains(s(u"Genre")));

    const TagUndo::Outcome out = undo.undo();
    QCOMPARE(out.restored, QStringList({ path }));
    QCOMPARE(out.failed, QStringList());
    QCOMPARE(changed.count(), 2);
    QVERIFY(!undo.canUndo());
    QCOMPARE(undo.name(), QString());
    QCOMPARE(undo.paths(), QStringList());
    const TrackTags after = tagsOf(path);
    QCOMPARE(after.fields, before.fields); // every field as it was, the absent ones absent again
    QCOMPARE(after.other, before.other);
    QCOMPARE(after.pictures.size(), before.pictures.size());
    // There is no redo, and a second undo does nothing.
    const TagUndo::Outcome again = undo.undo();
    QVERIFY(again.restored.isEmpty() && again.failed.isEmpty());
    QCOMPARE(changed.count(), 2);
    QCOMPARE(tagsOf(path).fields, before.fields);
}

void TagUndoTest::onlyWhatWasNamed()
{
    // UN-RECORD 3, UN-STEP: entries the change did not name are not touched by undo. The writer
    // here does more than it is asked: it changes the album beside the title that was named.
    const QString path = copy("basic.flac");
    TagUndo undo(m_scratch);
    undo.begin(s(u"Capitalise"));
    const TagUndo::Writer more = [](const QString &file, const TagChanges &changes) {
        TagChanges all = changes;
        all.fields.insert(QStringLiteral("Album"), { QStringLiteral("Changed Beside It") });
        return writeTags(file, all);
    };
    QVERIFY(undo.write(path, fields({ { s(u"Title"), { s(u"Changed") } } }), more).status
        == Status::Ok);
    undo.end();
    QCOMPARE(tagsOf(path).fields.value(s(u"Album")), QStringList({ s(u"Changed Beside It") }));
    const QString titleBefore = s(u"Flac Title");
    QCOMPARE(undo.undo().restored, QStringList({ path }));
    QCOMPARE(tagsOf(path).fields.value(s(u"Title")),
        QStringList({ titleBefore })); // what it held before the step
    QCOMPARE(tagsOf(path).fields.value(s(u"Album")),
        QStringList({ s(u"Changed Beside It") })); // not named: left
}

void TagUndoTest::oneStep()
{
    // UN-STEP: the next change forgets the one before; a change that kept nothing does not.
    const QString a = copy("basic.flac", "a.flac");
    const QString b = copy("basic.mp3", "b.mp3");
    const QString c = copy("basic.ogg", "c.ogg");
    const QStringList titleA = tagsOf(a).fields.value(s(u"Title"));
    const QStringList titleB = tagsOf(b).fields.value(s(u"Title"));
    TagUndo undo(m_scratch);
    QSignalSpy changed(&undo, &TagUndo::changed);
    undo.begin(s(u"First"));
    QVERIFY(undo.write(b, fields({ { s(u"Title"), { s(u"1b") } } })).ok());
    QVERIFY(undo.write(a, fields({ { s(u"Title"), { s(u"1a") } } })).ok());
    undo.end();
    QCOMPARE(undo.paths(), QStringList({ b, a })); // the order written
    // A change that keeps nothing: cancelled (no write), or nothing differs.
    undo.begin(s(u"Cancelled"));
    QVERIFY(!undo.canUndo()); // not while a change is open, though the earlier step is still kept
    QCOMPARE(undo.name(), QString());
    QCOMPARE(undo.paths(), QStringList());
    undo.end();
    QVERIFY(undo.canUndo());
    undo.begin(s(u"Nothing Differs"));
    QVERIFY(undo.write(a, fields({ { s(u"Title"), { s(u"1a") } } })).status == Status::Unchanged);
    undo.end();
    QCOMPARE(undo.name(), s(u"First"));
    QCOMPARE(undo.paths(), QStringList({ b, a }));
    QCOMPARE(changed.count(), 1);
    // A second change: now the first is forgotten.
    undo.begin(s(u"Second"));
    QVERIFY(undo.write(c, fields({ { s(u"Title"), { s(u"2c") } } })).ok());
    QCOMPARE(undo.name(), QString()); // a change is open
    undo.end();
    QCOMPARE(undo.name(), s(u"Second"));
    QCOMPARE(undo.paths(), QStringList({ c }));
    QCOMPARE(undo.undo().restored, QStringList({ c }));
    QCOMPARE(tagsOf(c).fields.value(s(u"Title")), QStringList({ s(u"Ogg Title") }));
    QCOMPARE(tagsOf(a).fields.value(s(u"Title")),
        QStringList({ s(u"1a") })); // the first step is gone for good
    QCOMPARE(tagsOf(b).fields.value(s(u"Title")), QStringList({ s(u"1b") }));
    QVERIFY(!undo.canUndo());
    QVERIFY(titleA != QStringList({ s(u"1a") }) && titleB != QStringList({ s(u"1b") }));
}

void TagUndoTest::picturesComeBack()
{
    // UN-RECORD 5: removed pictures are kept as files, one for each different picture, and come
    // back.
    QStringList paths;
    for (const char *name : { "p1.flac", "p2.flac", "p3.flac" })
        paths.append(copy("basic.flac", name));
    const QString other = copy("basic.mp3", "p4.mp3");
    TagChanges give;
    give.pictures = QList<Picture> { cover() };
    for (const QString &p : paths)
        QVERIFY(writeTags(p, give).ok());
    TagChanges two;
    two.pictures = QList<Picture> { cover(), cover("another") };
    QVERIFY(writeTags(other, two).ok());
    const QStringList titles = tagsOf(paths.at(0)).fields.value(s(u"Title"));

    {
        TagUndo undo(m_scratch);
        QVERIFY(!QFileInfo::exists(m_scratch) || kept().isEmpty()); // nothing until it is needed
        undo.begin(s(u"Remove Tags"));
        TagChanges remove;
        remove.pictures = QList<Picture> { };
        remove.fields.insert(s(u"Title"), { });
        for (const QString &p : paths + QStringList({ other }))
            QVERIFY(undo.write(p, remove).status == Status::Ok);
        undo.end();
        QCOMPARE(tagsOf(paths.at(0)).pictures.size(), 0);
        QCOMPARE(kept().size(), 2); // two different pictures in four files
        qint64 bytes = 0;
        for (const QString &name : kept())
            bytes += QFileInfo(m_scratch + QLatin1Char('/') + name).size();
        QCOMPARE(bytes, qint64(png().size() + png("another").size()));

        QCOMPARE(undo.undo().restored, paths + QStringList({ other }));
        for (const QString &p : paths) {
            const TrackTags t = tagsOf(p);
            QCOMPARE(t.pictures.size(), 1);
            QCOMPARE(t.pictures.at(0).data, png());
            QCOMPARE(t.pictures.at(0).type, s(u"Front Cover"));
            QCOMPARE(t.fields.value(s(u"Title")), titles);
        }
        const TrackTags t = tagsOf(other);
        QCOMPARE(t.pictures.size(), 2);
        QCOMPARE(t.pictures.at(0).data, png());
        QCOMPARE(t.pictures.at(1).data, png("another"));
        QCOMPARE(kept(), QStringList()); // the step is forgotten, and its files with it

        // Kept again, and forgotten by the next step, and by the object's end.
        undo.begin(s(u"Remove Tags"));
        QVERIFY(undo.write(paths.at(0), remove).ok());
        undo.end();
        QCOMPARE(kept().size(), 1);
        undo.begin(s(u"Edit Tags"));
        QVERIFY(undo.write(paths.at(1), fields({ { s(u"Title"), { s(u"x") } } })).ok());
        undo.end();
        QCOMPARE(kept(), QStringList());
        undo.begin(s(u"Remove Tags"));
        QVERIFY(undo.write(paths.at(2), remove).ok());
        undo.end();
        QCOMPARE(kept().size(), 1);
        // A file of somebody else's in the folder is never removed.
        QFile mine(m_scratch + QStringLiteral("/not-of-the-undo.txt"));
        QVERIFY(mine.open(QIODevice::WriteOnly));
        mine.write("mine");
    }
    QCOMPARE(kept(), QStringList({ s(u"not-of-the-undo.txt") }));
    QVERIFY(QFileInfo(m_scratch).isDir()); // the folder is left
}

void TagUndoTest::twiceInOneStep()
{
    // UN-RECORD 4: a file written twice keeps what each entry held before the first write that
    // named it.
    const QString path = copy("basic.flac");
    const TrackTags before = tagsOf(path);
    TagUndo undo(m_scratch);
    undo.begin(s(u"Two Passes"));
    QVERIFY(undo.write(path, fields({ { s(u"Title"), { s(u"first") } } })).ok());
    QVERIFY(
        undo.write(path,
                fields(
                    { { s(u"Title"), { s(u"second") } }, { s(u"Album"), { s(u"second album") } } }))
            .ok());
    undo.end();
    QCOMPARE(undo.paths(), QStringList({ path }));
    QCOMPARE(undo.undo().restored, QStringList({ path }));
    QCOMPARE(tagsOf(path).fields, before.fields);
}

void TagUndoTest::whatIsNotKept()
{
    // UN-RECORD: nothing for a result that is not Ok; nothing outside begin and end; the writer's
    // result comes back as it is.
    const QString path = copy("basic.flac");
    const QStringList title = tagsOf(path).fields.value(s(u"Title"));
    TagUndo undo(m_scratch);
    QSignalSpy changed(&undo, &TagUndo::changed);
    QVERIFY(undo.write(path, fields({ { s(u"Title"), { s(u"outside") } } })).status
        == Status::Ok); // written
    QCOMPARE(tagsOf(path).fields.value(s(u"Title")), QStringList({ s(u"outside") }));
    QVERIFY(!undo.canUndo());

    int calls = 0;
    const TagUndo::Writer refuses = [&calls](const QString &, const TagChanges &) {
        ++calls;
        WriteResult r;
        r.status = Status::NotWritable;
        r.problems = QStringList({ QStringLiteral("said by the test") });
        return r;
    };
    undo.begin(s(u"Refused"));
    const WriteResult r = undo.write(path, fields({ { s(u"Title"), { s(u"never") } } }), refuses);
    QVERIFY(r.status == Status::NotWritable);
    QCOMPARE(r.problems, QStringList({ s(u"said by the test") }));
    QCOMPARE(calls, 1);
    undo.end();
    QVERIFY(!undo.canUndo());
    QCOMPARE(changed.count(), 0);
    // The writer is given the path and the change as they are.
    QString gotPath;
    TagChanges got;
    undo.begin(s(u"Seen"));
    const TagChanges change = fields({ { s(u"Title"), { s(u"seen") } } });
    undo.write(path, change, [&](const QString &p, const TagChanges &c) {
        gotPath = p;
        got = c;
        return writeTags(p, c);
    });
    undo.end();
    QCOMPARE(gotPath, path);
    QCOMPARE(got.fields, change.fields);
    QVERIFY(!got.pictures.has_value());
    QVERIFY(undo.canUndo());
    // And undo goes through the writer it is given.
    QStringList undone;
    undo.undo([&](const QString &p, const TagChanges &c) {
        undone.append(p + QLatin1Char('|') + c.fields.value(s(u"Title")).join(QLatin1Char(',')));
        return writeTags(p, c);
    });
    QCOMPARE(undone, QStringList({ path + s(u"|outside") }));
}

void TagUndoTest::whenItCannotBeKept()
{
    // UN-RECORD: a change that could not be taken back is not made.
    TagUndo undo(m_scratch);
    int calls = 0;
    const TagUndo::Writer counts = [&calls](const QString &p, const TagChanges &c) {
        ++calls;
        return writeTags(p, c);
    };
    undo.begin(s(u"Unreadable"));
    WriteResult r = undo.write(m_dir->filePath(QStringLiteral("no-such.flac")),
        fields({ { s(u"Title"), { s(u"x") } } }), counts);
    QVERIFY(r.status == Status::Failed);
    QCOMPARE(r.problems, QStringList({ s(u"the earlier tags could not be read") }));
    r = undo.write(copy("notaudio.mp3"), fields({ { s(u"Title"), { s(u"x") } } }), counts);
    QVERIFY(r.status == Status::Failed);
    undo.end();
    QCOMPARE(calls, 0);
    QVERIFY(!undo.canUndo());

    // The scratch folder cannot be made (a file is in its place): a picture change is not written.
    const QString path = copy("basic.flac");
    TagChanges give;
    give.pictures = QList<Picture> { cover() };
    QVERIFY(writeTags(path, give).ok());
    const QString blocked = m_dir->filePath(QStringLiteral("blocked"));
    QFile file(blocked);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    TagUndo second(blocked);
    TagChanges remove;
    remove.pictures = QList<Picture> { };
    second.begin(s(u"Remove Tags"));
    r = second.write(path, remove, counts);
    QVERIFY(r.status == Status::Failed);
    QCOMPARE(r.problems, QStringList({ s(u"the earlier pictures could not be kept") }));
    // A change without pictures needs no folder and goes through.
    QVERIFY(second.write(path, fields({ { s(u"Title"), { s(u"fine") } } }), counts).status
        == Status::Ok);
    second.end();
    QCOMPARE(calls, 1);
    QCOMPARE(tagsOf(path).pictures.size(), 1);
    QVERIFY(second.canUndo());
}

void TagUndoTest::undoThatFails()
{
    // UN-STEP: a file that cannot be written is named; the others are put back; the step is
    // forgotten.
    const QString a = copy("basic.flac", "a.flac");
    const QString b = copy("basic.mp3", "b.mp3");
    const QString c = copy("basic.ogg", "c.ogg");
    TagUndo undo(m_scratch);
    undo.begin(s(u"Search and Replace"));
    for (const QString &p : { a, b, c })
        QVERIFY(undo.write(p, fields({ { s(u"Title"), { s(u"changed") } } })).ok());
    undo.end();
    QVERIFY(QFile::remove(b)); // gone meanwhile
    const TagUndo::Outcome out = undo.undo();
    QCOMPARE(out.restored, QStringList({ a, c }));
    QCOMPARE(out.failed, QStringList({ b }));
    QVERIFY(!undo.canUndo());
    QCOMPARE(tagsOf(a).fields.value(s(u"Title")), QStringList({ s(u"Flac Title") }));
    QCOMPARE(tagsOf(c).fields.value(s(u"Title")), QStringList({ s(u"Ogg Title") }));
}

QTEST_GUILESS_MAIN(TagUndoTest)
#include "tst_tagundo.moc"
