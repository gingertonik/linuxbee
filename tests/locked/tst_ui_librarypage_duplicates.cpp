// LOCKED ACCEPTANCE TEST (the duplicate box on the Library page). Written by the reviewer, not
// the builder. The builder makes these pass by changing src/, never by editing this file. A test
// believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/library-preferences.md (LP-STRUCTURE, LP-LOAD, LP-APPLY)
// and docs/spec/playlist-editing.md (PE-DUPLICATES). W-GP: MusicBee's "General Preferences" has
// the box "warn when duplicate tracks are added to a playlist", ticked by default. Runs on the
// offscreen platform, with a settings file in a temporary folder of its own.
#include <QCheckBox>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include "preferences/librarypage.h"

using namespace linuxbee::ui;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

const QString kKey = QStringLiteral("playlists/warnOnDuplicates");

// A settings file as the next run of the program reads it: every value is text.
QString nextRun(QSettings &settings)
{
    static int count = 0;
    settings.sync();
    const QString copy = settings.fileName() + QStringLiteral(".run%1.ini").arg(++count);
    QFile::remove(copy);
    QFile::copy(settings.fileName(), copy);
    return copy;
}

QCheckBox *box(QWidget &page)
{
    auto *found = page.findChild<QCheckBox *>(s(u"warnDuplicatesBox"));
    if (!found)
        QTest::qFail("no widget named warnDuplicatesBox", __FILE__, __LINE__);
    return found;
}

}

class LibraryPageDuplicatesTest : public QObject {
    Q_OBJECT
private slots:
    void structure();
    void loads_data();
    void loads();
    void appliesAndForgets();
};

void LibraryPageDuplicatesTest::structure()
{
    // LP-STRUCTURE: the box, with words of linuxBee's own (after W-GP); checked when nothing is
    // stored.
    QTemporaryDir dir;
    QSettings settings(dir.path() + s(u"/s.ini"), QSettings::IniFormat);
    LibraryPage page(&settings);
    QCheckBox *b = box(page);
    QVERIFY(b);
    QCOMPARE(b->text(), s(u"Warn when adding tracks already in a playlist"));
    QVERIFY(b->isChecked());
    QVERIFY(!page.isModified());
}

void LibraryPageDuplicatesTest::loads_data()
{
    QTest::addColumn<QString>("stored");
    QTest::addColumn<bool>("checked");
    QTest::newRow("false") << s(u"false") << false;
    QTest::newRow("FALSE") << s(u"FALSE") << false;
    QTest::newRow("0") << s(u"0") << false;
    QTest::newRow("true") << s(u"true") << true;
    QTest::newRow("1") << s(u"1") << true;
    QTest::newRow("perhaps") << s(u"perhaps") << true;
    QTest::newRow("empty") << QString() << true;
}

void LibraryPageDuplicatesTest::loads()
{
    // LP-LOAD: checked unless the stored text is false (in any case of letters) or 0.
    QFETCH(QString, stored);
    QFETCH(bool, checked);
    QTemporaryDir dir;
    QSettings settings(dir.path() + s(u"/s.ini"), QSettings::IniFormat);
    settings.setValue(kKey, stored);
    QSettings later(nextRun(settings), QSettings::IniFormat);
    LibraryPage page(&later);
    QCOMPARE(box(page)->isChecked(), checked);
}

void LibraryPageDuplicatesTest::appliesAndForgets()
{
    // LP-APPLY: the box is part of what is modified and what is stored; load() forgets an edit.
    QTemporaryDir dir;
    QSettings settings(dir.path() + s(u"/s.ini"), QSettings::IniFormat);
    int stored = 0;
    LibraryPage::Hooks hooks;
    hooks.stored = [&] { ++stored; };
    LibraryPage page(&settings, hooks);
    box(page)->setChecked(false);
    QVERIFY(page.isModified());
    QCOMPARE(settings.allKeys(), QStringList()); // nothing stored by editing
    page.load();
    QVERIFY(box(page)->isChecked());
    QVERIFY(!page.isModified());
    box(page)->setChecked(false);
    QCOMPARE(page.apply(), QString());
    QCOMPARE(stored, 1);
    QVERIFY(!page.isModified());
    QSettings later(nextRun(settings), QSettings::IniFormat);
    QCOMPARE(later.value(kKey).toString(), s(u"false"));
    QVERIFY(later.contains(s(u"library/searchIgnoresAccents"))); // the page's other keys, as ever
    box(page)->setChecked(true);
    page.apply();
    QCOMPARE(stored, 2);
    QSettings again(nextRun(settings), QSettings::IniFormat);
    QCOMPARE(again.value(kKey).toString(), s(u"true"));
    // The other box is its own: changing one leaves the other.
    QCheckBox *accents = page.findChild<QCheckBox *>(s(u"ignoreAccentsBox"));
    QVERIFY(accents);
    accents->setChecked(false);
    page.apply();
    QSettings third(nextRun(settings), QSettings::IniFormat);
    QCOMPARE(third.value(kKey).toString(), s(u"true"));
    QCOMPARE(third.value(s(u"library/searchIgnoresAccents")).toString(), s(u"false"));
}

QTEST_MAIN(LibraryPageDuplicatesTest)
#include "tst_ui_librarypage_duplicates.moc"
