// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Every expected value is a rule in docs/spec/auto-playlists.md (A-x) worked
// by hand on the eight tracks below. W-AP is the MusicBee wiki's
// "Auto-Playlists" page, which names the options and their defaults.
#include <QtTest>

#include "playlists/autoplaylist.h"

using linuxbee::AutoPlaylist;
using linuxbee::LibraryTrack;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

QDateTime day(int d)
{
    return QDateTime(QDate(2026, 11, d), QTime(12, 0), QTimeZone::UTC);
}

LibraryTrack make(qint64 id, const char16_t *artist, const char16_t *title, const char16_t *album,
    const char16_t *rating, int minutes, int megabytes, int plays, int lastPlayedDay, int addedDay)
{
    LibraryTrack t;
    t.id = id;
    t.path = QStringLiteral("/m/%1.mp3").arg(id);
    t.category = s(u"Music");
    t.tags.fields.insert(s(u"Artist"), { s(artist) });
    t.tags.fields.insert(s(u"Album Artist"), { s(artist) });
    t.tags.fields.insert(s(u"Title"), { s(title) });
    t.tags.fields.insert(s(u"Album"), { s(album) });
    if (rating)
        t.tags.fields.insert(s(u"Rating"), { s(rating) });
    t.tags.audio.durationMs = minutes * 60000;
    t.sizeBytes = qint64(megabytes) * 1048576;
    t.playCount = plays;
    if (lastPlayedDay)
        t.lastPlayed = day(lastPlayedDay);
    t.dateAdded = day(addedDay);
    return t;
}

//  id artist  title        album   rating min MB plays lastPlayed added
QList<LibraryTrack> library()
{
    QList<LibraryTrack> l = {
        make(1, u"Adele", u"Hello", u"25", u"5", 5, 10, 9, 10, 1),
        make(2, u"Adele", u"Skyfall", u"Skyfall", u"3", 5, 10, 2, 12, 2),
        make(3, u"Muse", u"Uprising", u"The Resistance", u"4.5", 5, 20, 9, 3, 3),
        make(4, u"Muse", u"Resistance", u"The Resistance", nullptr, 6, 20, 0, 0, 4),
        make(5, u"ADELE", u" hello ", u"Live", u"4", 4, 30, 1, 5, 5), // the same song as 1, by A-DUP
        make(6, u"Bj\u00f6rk", u"J\u00f3ga", u"Homogenic", u"5", 5, 5, 4, 11, 6),
        make(7, u"Bjork", u"Joga", u"Homogenic", u"2", 5, 5, 0, 0, 7), // and the same as 6
        make(8, u"Muse", u"Gone", u"Absolution", u"5", 4, 1, 3, 1, 8),
    };
    l[7].missing = true; // A-MATCH: never in a playlist
    return l;
}

linuxbee::RuleContext now()
{
    linuxbee::RuleContext c;
    c.now = day(13);
    return c;
}

QList<qint64> run(const AutoPlaylist &p, quint32 seed = 1)
{
    return linuxbee::evaluateAutoPlaylist(p, library(), now(), seed);
}

AutoPlaylist by(AutoPlaylist::SelectedBy selectedBy)
{
    AutoPlaylist p;
    p.selectedBy = selectedBy;
    return p;
}

linuxbee::Rule rule(const char16_t *field, linuxbee::RuleOperator op, const QStringList &values)
{
    linuxbee::Rule r;
    r.field = s(field);
    r.op = op;
    r.values = values;
    return r;
}

}

class AutoPlaylistTest : public QObject {
    Q_OBJECT
private slots:
    void names();
    void selectedBy();
    void rules();
    void random();
    void limits();
    void duplicates();
    void json();
    void jsonErrors_data();
    void jsonErrors();
};

void AutoPlaylistTest::names()
{
    // A-NAMES
    using U = AutoPlaylist::LimitUnit;
    using S = AutoPlaylist::SelectedBy;
    QStringList units, selects;
    for (const U u : { U::Items, U::Minutes, U::Hours, U::Megabytes, U::Gigabytes, U::Albums })
        units.append(linuxbee::limitUnitId(u));
    QCOMPARE(units.join(QLatin1Char(' ')), s(u"items minutes hours megabytes gigabytes albums"));
    for (int i = 0; i <= int(S::LeastRecentlyAdded); ++i)
        selects.append(linuxbee::selectedById(S(i)));
    QCOMPARE(selects.join(QLatin1Char(' ')),
        s(u"random highest-rating lowest-rating most-recently-played least-recently-played most-often-played "
          u"least-often-played most-recently-added least-recently-added"));
    // W-AP defaults: no limit, 25 items when one is set, selected at random, duplicates kept.
    const AutoPlaylist p;
    QVERIFY(!p.limitEnabled && p.limitAmount == 25 && p.limitUnit == U::Items);
    QVERIFY(p.selectedBy == S::Random && !p.filterDuplicates);
}

void AutoPlaylistTest::selectedBy()
{
    using S = AutoPlaylist::SelectedBy;
    // A-SELECT: ties keep the order the tracks were given in. Track 8 is missing.
    // ratings: 1=5, 6=5, 3=4.5, 5=4, 2=3, 7=2, 4=none (counts as 0)
    QCOMPARE(run(by(S::HighestRating)), QList<qint64>({ 1, 6, 3, 5, 2, 7, 4 }));
    QCOMPARE(run(by(S::LowestRating)), QList<qint64>({ 4, 7, 2, 5, 3, 1, 6 }));
    // last played on day: 2=12, 6=11, 1=10, 5=5, 3=3; 4 and 7 never (last in both directions)
    QCOMPARE(run(by(S::MostRecentlyPlayed)), QList<qint64>({ 2, 6, 1, 5, 3, 4, 7 }));
    QCOMPARE(run(by(S::LeastRecentlyPlayed)), QList<qint64>({ 4, 7, 3, 5, 1, 6, 2 }));
    // plays: 1=9, 3=9, 6=4, 2=2, 5=1, 4=0, 7=0
    QCOMPARE(run(by(S::MostOftenPlayed)), QList<qint64>({ 1, 3, 6, 2, 5, 4, 7 }));
    QCOMPARE(run(by(S::LeastOftenPlayed)), QList<qint64>({ 4, 7, 5, 2, 6, 1, 3 }));
    QCOMPARE(run(by(S::MostRecentlyAdded)), QList<qint64>({ 7, 6, 5, 4, 3, 2, 1 }));
    QCOMPARE(run(by(S::LeastRecentlyAdded)), QList<qint64>({ 1, 2, 3, 4, 5, 6, 7 }));
    QVERIFY(linuxbee::evaluateAutoPlaylist(by(S::HighestRating), { }, now(), 1).isEmpty());
}

void AutoPlaylistTest::rules()
{
    using S = AutoPlaylist::SelectedBy;
    // A-MATCH: the rules see every library field.
    AutoPlaylist p = by(S::LeastRecentlyAdded);
    p.rules.rules = { rule(u"Artist", linuxbee::RuleOperator::Is, { s(u"muse") }) };
    QCOMPARE(run(p), QList<qint64>({ 3, 4 })); // 8 is missing
    p.rules.rules = { rule(u"Play Count", linuxbee::RuleOperator::GreaterThan, { s(u"3") }),
        rule(u"Rating", linuxbee::RuleOperator::InRange, { s(u"4.5"), s(u"5") }) };
    QCOMPARE(run(p), QList<qint64>({ 1, 3, 6 }));
    p.rules.match = linuxbee::RuleSet::Match::Any;
    p.rules.rules = { rule(u"Last Played", linuxbee::RuleOperator::InTheLast, { s(u"2"), s(u"days") }),
        rule(u"Last Played", linuxbee::RuleOperator::HasNoValue, { }) };
    QCOMPARE(run(p), QList<qint64>({ 2, 4, 6, 7 })); // days 12 and 11 are within two days of day 13
    p.rules.rules = { rule(u"Category", linuxbee::RuleOperator::Is, { s(u"Podcasts") }) };
    QVERIFY(run(p).isEmpty());
    // Rules that do not validate: an empty playlist, not every track.
    p.rules.rules = { rule(u"Artist", linuxbee::RuleOperator::Matches, { s(u"(") }) };
    QVERIFY(run(p).isEmpty());
}

void AutoPlaylistTest::random()
{
    // A-SELECT random: every matching track once; the seed decides the order.
    const AutoPlaylist p;
    const QList<qint64> one = run(p, 1);
    QList<qint64> sorted = one;
    std::sort(sorted.begin(), sorted.end());
    QCOMPARE(sorted, QList<qint64>({ 1, 2, 3, 4, 5, 6, 7 }));
    QCOMPARE(run(p, 1), one);
    QSet<QList<qint64>> orders;
    for (quint32 seed = 1; seed <= 20; ++seed)
        orders.insert(run(p, seed));
    QVERIFY2(orders.size() >= 15, "twenty seeds should give nearly twenty orders of seven tracks");
    // Not simply the order given.
    int inOrder = 0;
    for (quint32 seed = 1; seed <= 20; ++seed)
        inOrder += run(p, seed) == QList<qint64>({ 1, 2, 3, 4, 5, 6, 7 }) ? 1 : 0;
    QVERIFY(inOrder <= 1);
    // The limit takes from the shuffled order, so different seeds pick different tracks.
    AutoPlaylist two = p;
    two.limitEnabled = true;
    two.limitAmount = 2;
    QSet<QList<qint64>> picks;
    for (quint32 seed = 1; seed <= 20; ++seed) {
        QCOMPARE(run(two, seed), run(p, seed).mid(0, 2));
        picks.insert(run(two, seed));
    }
    QVERIFY(picks.size() >= 8);
}

void AutoPlaylistTest::limits()
{
    using S = AutoPlaylist::SelectedBy;
    using U = AutoPlaylist::LimitUnit;
    AutoPlaylist p = by(S::LeastRecentlyAdded); // order 1..7; minutes 5,5,5,6,4,5,5; MB 10,10,20,20,30,5,5
    p.limitEnabled = true;
    auto with = [&p](int amount, U unit) {
        p.limitAmount = amount;
        p.limitUnit = unit;
        return run(p);
    };
    // A-LIMIT items
    QCOMPARE(with(3, U::Items), QList<qint64>({ 1, 2, 3 }));
    QCOMPARE(with(99, U::Items), QList<qint64>({ 1, 2, 3, 4, 5, 6, 7 }));
    // time and size: tracks are taken in order until the next one would go over
    QCOMPARE(with(15, U::Minutes), QList<qint64>({ 1, 2, 3 })); // exactly 15
    QCOMPARE(with(20, U::Minutes), QList<qint64>({ 1, 2, 3 })); // track 4 would make 21, and nothing after it is tried
    QCOMPARE(with(4, U::Minutes), QList<qint64>({ }));
    QCOMPARE(with(1, U::Hours), QList<qint64>({ 1, 2, 3, 4, 5, 6, 7 })); // 35 minutes in all
    QCOMPARE(with(40, U::Megabytes), QList<qint64>({ 1, 2, 3 }));
    QCOMPARE(with(39, U::Megabytes), QList<qint64>({ 1, 2 }));
    QCOMPARE(with(1, U::Gigabytes), QList<qint64>({ 1, 2, 3, 4, 5, 6, 7 }));
    // albums: the first N albums met, with every chosen track of theirs, wherever it comes
    // albums in order: 25 (1), Skyfall (2), The Resistance (3, 4), Live (5), Homogenic (6 and 7:
    // an album is its Album Artist and Album with case and accents ignored)
    QCOMPARE(with(3, U::Albums), QList<qint64>({ 1, 2, 3, 4 }));
    QCOMPARE(with(1, U::Albums), QList<qint64>({ 1 }));
    p.selectedBy = S::HighestRating; // order 1, 6, 3, 5, 2, 7, 4
    // 25, Homogenic and The Resistance are in; 7 and 4 belong to them and follow in their places
    QCOMPARE(with(3, U::Albums), QList<qint64>({ 1, 6, 3, 7, 4 }));
    QCOMPARE(with(2, U::Albums), QList<qint64>({ 1, 6, 7 }));
    // a limit that is switched off is ignored; an amount below 1 gives nothing
    p.limitEnabled = false;
    p.limitAmount = 1;
    p.limitUnit = U::Items;
    QCOMPARE(run(p).size(), 7);
    p.limitEnabled = true;
    p.limitAmount = 0;
    QVERIFY(run(p).isEmpty());
}

void AutoPlaylistTest::duplicates()
{
    using S = AutoPlaylist::SelectedBy;
    // A-DUP: the same artist and title, ignoring case, accents and outer spaces.
    // The first one in the order the tracks were given is the one kept, before any selection.
    AutoPlaylist p = by(S::LeastRecentlyAdded);
    p.filterDuplicates = true;
    QCOMPARE(run(p), QList<qint64>({ 1, 2, 3, 4, 6 }));
    p.selectedBy = S::MostRecentlyAdded;
    QCOMPARE(run(p), QList<qint64>({ 6, 4, 3, 2, 1 })); // still 1 and 6, not 5 and 7
    // Only tracks that pass the rules count as "first".
    p.rules.rules = { rule(u"Album", linuxbee::RuleOperator::IsAnyOf, { s(u"Live"), s(u"Homogenic") }) };
    QCOMPARE(run(p), QList<qint64>({ 6, 5 }));
}

void AutoPlaylistTest::json()
{
    // A-JSON
    QCOMPARE(linuxbee::autoPlaylistToJson(AutoPlaylist()),
        s(u"{\"version\":1,\"rules\":{\"match\":\"all\",\"rules\":[]},\"limit\":null,"
          u"\"selectedBy\":\"random\",\"filterDuplicates\":false}"));
    AutoPlaylist p;
    p.rules.match = linuxbee::RuleSet::Match::Any;
    p.rules.rules = { rule(u"Genre", linuxbee::RuleOperator::Is, { s(u"Rock") }) };
    p.limitEnabled = true;
    p.limitAmount = 90;
    p.limitUnit = AutoPlaylist::LimitUnit::Minutes;
    p.selectedBy = AutoPlaylist::SelectedBy::LeastOftenPlayed;
    p.filterDuplicates = true;
    const QString text = s(u"{\"version\":1,\"rules\":{\"match\":\"any\",\"rules\":[{\"field\":\"Genre\",\"op\":\"is\","
                           u"\"values\":[\"Rock\"]}]},\"limit\":{\"amount\":90,\"unit\":\"minutes\"},"
                           u"\"selectedBy\":\"least-often-played\",\"filterDuplicates\":true}");
    QCOMPARE(linuxbee::autoPlaylistToJson(p), text);
    AutoPlaylist back;
    QString error;
    QVERIFY2(linuxbee::autoPlaylistFromJson(text, &back, &error), qPrintable(error));
    QCOMPARE(linuxbee::autoPlaylistToJson(back), text);
    // A limit that is off is not stored, so its amount and unit come back as the defaults.
    p.limitEnabled = false;
    QVERIFY(linuxbee::autoPlaylistFromJson(linuxbee::autoPlaylistToJson(p), &back, &error));
    QVERIFY(!back.limitEnabled && back.limitAmount == 25 && back.limitUnit == AutoPlaylist::LimitUnit::Items);
    // Everything may be left out.
    QVERIFY(linuxbee::autoPlaylistFromJson(s(u" { } "), &back, &error));
    QCOMPARE(linuxbee::autoPlaylistToJson(back), linuxbee::autoPlaylistToJson(AutoPlaylist()));
    QVERIFY(linuxbee::autoPlaylistFromJson(s(u"{\"limit\":null,\"version\":1}"), &back, nullptr));
}

void AutoPlaylistTest::jsonErrors_data()
{
    QTest::addColumn<QString>("json");
    QTest::newRow("empty") << QString();
    QTest::newRow("array") << s(u"[]");
    QTest::newRow("newer") << s(u"{\"version\":2}");
    QTest::newRow("unknown-key") << s(u"{\"sort\":\"Title\"}");
    QTest::newRow("rules-not-object") << s(u"{\"rules\":[]}");
    QTest::newRow("rules-bad") << s(u"{\"rules\":{\"match\":\"some\"}}");
    QTest::newRow("limit-not-object") << s(u"{\"limit\":25}");
    QTest::newRow("limit-no-unit") << s(u"{\"limit\":{\"amount\":25}}");
    QTest::newRow("limit-bad-unit") << s(u"{\"limit\":{\"amount\":25,\"unit\":\"tracks\"}}");
    QTest::newRow("limit-zero") << s(u"{\"limit\":{\"amount\":0,\"unit\":\"items\"}}");
    QTest::newRow("limit-fraction") << s(u"{\"limit\":{\"amount\":2.5,\"unit\":\"items\"}}");
    QTest::newRow("limit-text") << s(u"{\"limit\":{\"amount\":\"25\",\"unit\":\"items\"}}");
    QTest::newRow("selected-bad") << s(u"{\"selectedBy\":\"best\"}");
    QTest::newRow("duplicates-not-bool") << s(u"{\"filterDuplicates\":1}");
}

void AutoPlaylistTest::jsonErrors()
{
    QFETCH(QString, json);
    AutoPlaylist p;
    p.limitAmount = 77;
    QString error;
    QVERIFY(!linuxbee::autoPlaylistFromJson(json, &p, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(p.limitAmount, 77); // left alone on failure
    QVERIFY(!linuxbee::autoPlaylistFromJson(json, &p, nullptr));
}

QTEST_GUILESS_MAIN(AutoPlaylistTest)
#include "tst_autoplaylist.moc"
