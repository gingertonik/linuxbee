// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// Love: read from a file, written to one, and usable as a field.
// Expected values: docs/spec/tag-reading.md (R-CATALOGUE, R-LOVE), docs/spec/tag-writing.md
// (W-TABLE, W-INVALID, W-ONEHOME). The fixtures were written by
// tests/data/tags/make_love_fixtures.py, with a tagging library that is not the program's.
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "fields/trackfields.h"
#include "rules/rules.h"
#include "tags/tagreader.h"
#include "tags/tagwriter.h"

using namespace linuxbee;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

const QString kLove = QStringLiteral("Love");
const QString kSpaced = QStringLiteral("LOVE RATING");
const QString kJoined = QStringLiteral("LOVERATING");

QString fixture(const QString &name)
{
    return QStringLiteral(LINUXBEE_TEST_DATA "/tags/") + name;
}

TrackTags tagsOf(const QString &path)
{
    const ReadResult read = readTags(path);
    return read.status == ReadResult::Status::Ok ? read.tags : TrackTags { };
}

// How often `key` stands in the file as a tag's name: as plain bytes, or as UTF-16 of either order
// (an ID3v2 frame may hold its description so).
int timesInFile(const QString &path, const QString &key)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return -1;
    const QByteArray bytes = file.readAll();
    QByteArray little;
    QByteArray big;
    for (const QChar c : key) {
        little.append(char(c.unicode())).append('\0');
        big.append('\0').append(char(c.unicode()));
    }
    return int(bytes.count(key.toLatin1()) + bytes.count(little) + bytes.count(big));
}

bool matches(const QString &json, const TrackTags &tags)
{
    RuleSet rules;
    QString error;
    if (!rulesFromJson(json, &rules, &error) || !validateRules(rules).isEmpty())
        return false;
    const TrackFieldSource source(tags, QStringLiteral("/music/a.flac"));
    return matchesRules(rules, source, RuleContext { });
}

}

class TagsLoveTest : public QObject {
    Q_OBJECT
private slots:
    void init() { QVERIFY(m_dir.isValid()); }
    void theFieldIsLastInTheCatalogue();
    void read_data();
    void read();
    void aValueThatIsNotOneStaysAnOtherTag();
    void theFirstNameWinsAndTheOtherStays();
    void aNameThatHoldsNoLoveValueIsPassedOver();
    void written_data();
    void written();
    void aWriteLeavesOneNameOnly();
    void refused_data();
    void refused();
    void itsNamesAreNotOtherTags_data();
    void itsNamesAreNotOtherTags();
    void aRuleCanAskForIt();
    void aTemplateFieldHasIt();

private:
    QString copy(const QString &name);
    QTemporaryDir m_dir;
    int m_counter = 0;
};

// A fresh, writable copy of a fixture. The fixtures themselves are never opened for writing.
QString TagsLoveTest::copy(const QString &name)
{
    const QString target = m_dir.filePath(QStringLiteral("%1-%2").arg(++m_counter).arg(name));
    if (!QFile::copy(fixture(name), target))
        return { };
    QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner);
    return target;
}

void TagsLoveTest::theFieldIsLastInTheCatalogue()
{
    const QStringList names = tagFieldNames();
    QVERIFY(names.size() >= 2);
    QCOMPARE(names.last(), kLove);
    QCOMPARE(names.at(names.size() - 2), s(u"Mix Artist"));
    QCOMPARE(names.count(kLove), 1);
    QVERIFY(trackFieldNames().contains(kLove));
}

void TagsLoveTest::read_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("value");
    QTest::addColumn<QString>("madeFrom");
    QTest::newRow("mp3, a TXXX frame") << s(u"love.mp3") << s(u"L") << s(u"basic.mp3");
    QTest::newRow("flac") << s(u"love.flac") << s(u"L") << s(u"basic.flac");
    QTest::newRow("ogg") << s(u"love.ogg") << s(u"L") << s(u"basic.ogg");
    QTest::newRow("m4a, the name without its space")
        << s(u"love.m4a") << s(u"L") << s(u"basic.m4a");
    QTest::newRow("ban") << s(u"ban.flac") << s(u"B") << s(u"basic.flac");
    QTest::newRow("lower case, a space after")
        << s(u"love-lower.flac") << s(u"L") << s(u"basic.flac");
}

void TagsLoveTest::read()
{
    QFETCH(QString, name);
    QFETCH(QString, value);
    QFETCH(QString, madeFrom);
    const ReadResult result = readTags(fixture(name));
    QCOMPARE(result.status, ReadResult::Status::Ok);
    QCOMPARE(result.tags.fields.value(kLove), QStringList { value });
    QVERIFY2(!result.tags.other.contains(kSpaced), "the field's own name is not also an other tag");
    QVERIFY(!result.tags.other.contains(kJoined));
    // Nothing else is read differently: but for Love, the tags are those of the file it was made
    // from.
    TrackTags rest = result.tags;
    rest.fields.remove(kLove);
    const TrackTags basis = tagsOf(fixture(madeFrom));
    QVERIFY(!basis.fields.isEmpty());
    QCOMPARE(rest.fields, basis.fields);
    QCOMPARE(rest.other, basis.other);
}

void TagsLoveTest::aValueThatIsNotOneStaysAnOtherTag()
{
    const TrackTags tags = tagsOf(fixture(s(u"love-odd.flac")));
    QVERIFY(!tags.fields.contains(kLove));
    QCOMPARE(tags.other.value(kSpaced), QStringList { s(u"Yes") });
    // And a file with none has none.
    const TrackTags plain = tagsOf(fixture(s(u"basic.flac")));
    QVERIFY(!plain.fields.contains(kLove));
    QVERIFY(!plain.other.contains(kSpaced));
}

void TagsLoveTest::theFirstNameWinsAndTheOtherStays()
{
    const TrackTags tags = tagsOf(fixture(s(u"love-both.flac")));
    QCOMPARE(tags.fields.value(kLove), QStringList { s(u"L") });
    QVERIFY(!tags.other.contains(kSpaced));
    QCOMPARE(tags.other.value(kJoined), QStringList { s(u"B") }); // R-OTHER, the losing key
}

// R-LOVE: a file tagged by two programs. The first name holds a word, the second a Love value.
void TagsLoveTest::aNameThatHoldsNoLoveValueIsPassedOver()
{
    const TrackTags tags = tagsOf(fixture(s(u"love-second.flac")));
    QCOMPARE(tags.fields.value(kLove), QStringList { s(u"L") });
    QCOMPARE(tags.other.value(kSpaced), QStringList { s(u"Yes") }); // not claimed, so it stays
    QVERIFY(!tags.other.contains(kJoined)); // claimed

    // A write clears both names and leaves one (W-ONEHOME).
    const QString path = copy(s(u"love-second.flac"));
    TagChanges ban;
    ban.fields.insert(kLove, { s(u"B") });
    QCOMPARE(writeTags(path, ban).status, WriteResult::Status::Ok);
    QCOMPARE(tagsOf(path).fields.value(kLove), QStringList { s(u"B") });
    QVERIFY(tagsOf(path).other.isEmpty() || !tagsOf(path).other.contains(kSpaced));
    QCOMPARE(timesInFile(path, kSpaced), 1);
    QCOMPARE(timesInFile(path, kJoined), 0);
}

void TagsLoveTest::written_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("key"); // W-TABLE
    QTest::addColumn<QString>("notKey");
    QTest::newRow("mp3") << s(u"basic.mp3") << kSpaced << kJoined;
    QTest::newRow("flac") << s(u"basic.flac") << kSpaced << kJoined;
    QTest::newRow("ogg") << s(u"basic.ogg") << kSpaced << kJoined;
    QTest::newRow("m4a") << s(u"basic.m4a") << kJoined << kSpaced;
}

void TagsLoveTest::written()
{
    QFETCH(QString, name);
    QFETCH(QString, key);
    QFETCH(QString, notKey);
    const QString path = copy(name);
    QVERIFY(!path.isEmpty());
    const TrackTags before = tagsOf(path);
    QCOMPARE(timesInFile(path, key), 0);

    TagChanges love;
    love.fields.insert(kLove, { s(u"L") });
    QCOMPARE(writeTags(path, love).status, WriteResult::Status::Ok);
    QCOMPARE(tagsOf(path).fields.value(kLove), QStringList { s(u"L") });
    QCOMPARE(timesInFile(path, key), 1);
    QCOMPARE(timesInFile(path, notKey), 0);
    // Said again, it is already so.
    QCOMPARE(writeTags(path, love).status, WriteResult::Status::Unchanged);

    // Lower case is taken and stored in upper case.
    TagChanges ban;
    ban.fields.insert(kLove, { s(u"b") });
    QCOMPARE(writeTags(path, ban).status, WriteResult::Status::Ok);
    QCOMPARE(tagsOf(path).fields.value(kLove), QStringList { s(u"B") });
    QCOMPARE(timesInFile(path, key), 1);

    // An empty list takes it out, and the file's tags are what they were.
    TagChanges none;
    none.fields.insert(kLove, { });
    QCOMPARE(writeTags(path, none).status, WriteResult::Status::Ok);
    QCOMPARE(timesInFile(path, key), 0);
    const TrackTags after = tagsOf(path);
    QCOMPARE(after.fields, before.fields);
    QCOMPARE(after.other, before.other);
}

// W-ONEHOME: a file that carried both names carries one after a write.
void TagsLoveTest::aWriteLeavesOneNameOnly()
{
    const QString path = copy(s(u"love-both.flac"));
    TagChanges ban;
    ban.fields.insert(kLove, { s(u"B") });
    QCOMPARE(writeTags(path, ban).status, WriteResult::Status::Ok);
    const TrackTags tags = tagsOf(path);
    QCOMPARE(tags.fields.value(kLove), QStringList { s(u"B") });
    QVERIFY(!tags.other.contains(kJoined));
    QCOMPARE(timesInFile(path, kSpaced), 1);
    QCOMPARE(timesInFile(path, kJoined), 0);

    // What is in the file is the upper-case letter, whatever case was given (W-INVALID).
    const QString lower = copy(s(u"basic.flac"));
    TagChanges given;
    given.fields.insert(kLove, { s(u"b") });
    QCOMPARE(writeTags(lower, given).status, WriteResult::Status::Ok);
    QCOMPARE(timesInFile(lower, s(u"LOVE RATING=B")), 1);
    QCOMPARE(timesInFile(lower, s(u"LOVE RATING=b")), 0);

    // Nor is the white space about a letter written (W-INVALID).
    const QString spaced = copy(s(u"basic.flac"));
    TagChanges padded;
    padded.fields.insert(kLove, { s(u" l ") });
    QCOMPARE(writeTags(spaced, padded).status, WriteResult::Status::Ok);
    QCOMPARE(timesInFile(spaced, s(u"LOVE RATING=L")), 1);
    QCOMPARE(timesInFile(spaced, s(u"LOVE RATING= ")), 0);
    // The comment is the name, the sign and one letter: 13 bytes, as its length says.
    {
        QFile file(spaced);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray bytes = file.readAll();
        const qsizetype at = bytes.indexOf("LOVE RATING=");
        QVERIFY(at >= 4);
        QCOMPARE(int(uchar(bytes.at(at - 4))), 13);
    }

    // A value that was not one (it stood among the other tags) goes too.
    const QString odd = copy(s(u"love-odd.flac"));
    TagChanges love;
    love.fields.insert(kLove, { s(u"L") });
    QCOMPARE(writeTags(odd, love).status, WriteResult::Status::Ok);
    QCOMPARE(tagsOf(odd).fields.value(kLove), QStringList { s(u"L") });
    QVERIFY(!tagsOf(odd).other.contains(kSpaced));
    QCOMPARE(timesInFile(odd, kSpaced), 1);
}

void TagsLoveTest::refused_data()
{
    QTest::addColumn<QStringList>("values");
    QTest::newRow("a word") << QStringList { s(u"Yes") };
    QTest::newRow("two values") << QStringList { s(u"L"), s(u"B") };
    QTest::newRow("a number") << QStringList { s(u"1") };
    QTest::newRow("two letters") << QStringList { s(u"LB") };
}

void TagsLoveTest::refused()
{
    QFETCH(QStringList, values);
    const QString path = copy(s(u"love.flac"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    file.close();

    TagChanges changes;
    changes.fields.insert(kLove, values);
    changes.fields.insert(s(u"Title"), { s(u"Never written") });
    const WriteResult result = writeTags(path, changes);
    QCOMPARE(result.status, WriteResult::Status::InvalidChange);
    QCOMPARE(result.problems.size(), 1);
    QVERIFY2(result.problems.first().startsWith(kLove), qPrintable(result.problems.first()));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), bytes); // nothing was written, the title neither
}

void TagsLoveTest::itsNamesAreNotOtherTags_data()
{
    QTest::addColumn<QString>("key");
    QTest::newRow("with the space") << kSpaced;
    QTest::newRow("without") << kJoined;
    QTest::newRow("lower case") << s(u"love rating");
}

// W-INVALID: a key the catalogue reads is changed through its field.
void TagsLoveTest::itsNamesAreNotOtherTags()
{
    QFETCH(QString, key);
    const QString path = copy(s(u"basic.flac"));
    TagChanges changes;
    changes.other.insert(key, { s(u"L") });
    QCOMPARE(writeTags(path, changes).status, WriteResult::Status::InvalidChange);
    QVERIFY(!tagsOf(path).fields.contains(kLove));
}

void TagsLoveTest::aRuleCanAskForIt()
{
    const TrackTags loved = tagsOf(fixture(s(u"love.m4a")));
    const TrackTags banned = tagsOf(fixture(s(u"ban.flac")));
    const TrackTags plain = tagsOf(fixture(s(u"basic.flac")));
    const TrackTags odd = tagsOf(fixture(s(u"love-odd.flac")));
    const QString isLove = QString::fromUtf8(
        R"({"match":"all","rules":[{"field":"Love","op":"is","values":["L"]}]})");
    const QString hasNone = QString::fromUtf8(
        R"({"match":"all","rules":[{"field":"Love","op":"has-no-value","values":[]}]})");
    QVERIFY(matches(isLove, loved));
    QVERIFY(!matches(isLove, banned));
    QVERIFY(!matches(isLove, plain));
    QVERIFY(!matches(isLove, odd));
    QVERIFY(matches(hasNone, plain));
    QVERIFY(matches(hasNone, odd));
    QVERIFY(!matches(hasNone, loved));
    QVERIFY(!matches(hasNone, banned));
}

void TagsLoveTest::aTemplateFieldHasIt()
{
    const TrackTags loved = tagsOf(fixture(s(u"love.mp3")));
    const TrackFieldSource source(loved, QStringLiteral("/music/a.mp3"));
    QCOMPARE(source.value(kLove).value_or(QString()), s(u"L"));
    const TrackTags plain = tagsOf(fixture(s(u"basic.mp3")));
    const TrackFieldSource none(plain, QStringLiteral("/music/a.mp3"));
    QCOMPARE(none.value(kLove).value_or(QString()), QString());
}

QTEST_GUILESS_MAIN(TagsLoveTest)
#include "tst_tags_love.moc"
