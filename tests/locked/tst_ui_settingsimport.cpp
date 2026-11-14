// LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
// The builder makes these pass by changing src/, never by editing this file.
// A test believed to be wrong is reported, with the reason, and left alone.
//
// Expected values are rules in docs/spec/settings-import.md (SI-x). The
// fixture tests/data/settings/MusicBee3Settings.ini is invented, in the
// shape of the real file. The test is named tst_ui_ only so that it can ask
// Qt whether a key text is one it reads, and the command catalogue whether
// a command exists.
#include <QFile>
#include <QKeySequence>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include "actions/actionregistry.h"
#include "settingsimport/settingsimport.h"

using namespace linuxbee;

namespace {

QString s(const char16_t *text)
{
    return QString::fromUtf16(text);
}

QByteArray fixture()
{
    QFile file(QStringLiteral(LINUXBEE_TEST_DATA "/settings/MusicBee3Settings.ini"));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QByteArray wrapped(const QByteArray &inside)
{
    return "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n<ApplicationSettings>\r\n" + inside + "</ApplicationSettings>\r\n";
}

MusicBeeNamingPart part(int field, const char16_t *text, const char16_t *function = u"")
{
    MusicBeeNamingPart p;
    p.field = field;
    p.text = s(text);
    p.function = s(function);
    return p;
}

SettingsImportLine imported(const char16_t *what, const char16_t *key, const char16_t *value)
{
    return { s(what), s(key), s(value), QString() };
}

SettingsImportLine skipped(const char16_t *what, const char16_t *note)
{
    return { s(what), QString(), QString(), s(note) };
}

}

class SettingsImportTest : public QObject {
    Q_OBJECT
private slots:
    void reading();
    void readingOddFiles();
    void keys();
    void templates();
    void plan();
    void commandsExist();
    void applying();
};

void SettingsImportTest::reading()
{
    // SI-READ
    QVERIFY(fixture().size() > 1000);
    QVERIFY(fixture().contains('\x01')); // as the real file: not well-formed XML
    const MusicBeeSettings m = readMusicBeeSettings(fixture());
    QVERIFY2(m.ok, qPrintable(m.error));
    QCOMPARE(m.values.value(s(u"ENV_PromptLib")), s(u"false"));
    QCOMPARE(m.values.value(s(u"PlayerRepeatMode")), s(u"1"));
    QCOMPARE(m.values.value(s(u"PlayerVolume")), s(u"0.5"));
    QCOMPARE(m.values.value(s(u"ENV_OrigSetPath")), s(u"C:\\Users\\someone\\AppData\\Roaming\\MusicBee\\"));
    QVERIFY(m.values.contains(s(u"Empty1")) && m.values.value(s(u"Empty1")).isEmpty());
    QVERIFY(m.values.contains(s(u"Empty2")) && m.values.value(s(u"Empty2")).isEmpty());
    QCOMPARE(m.values.value(s(u"Entities")), s(u"a & b <c> \"d\" \u00e9"));
    QCOMPARE(m.values.value(s(u"Twice")), s(u"second"));
    QCOMPARE(m.values.value(s(u"LastOne")), s(u"42")); // what follows the odd line is still read
    QCOMPARE(m.values.value(s(u"ArtistSortMapping")), s(u"Some Artist Artist, Some Another One One, Another")); // U+0001 as a space
    for (const char16_t *list : { u"HotKeys", u"SystemPlugin", u"OrganisationDefaultFilenameRules", u"KeyCommandPair", u"State", u"Id", u"Key" })
        QVERIFY2(!m.values.contains(s(list)), qPrintable(s(list)));

    QCOMPARE(m.hotkeys.size(), 17); // the pair with no number and the pair with no command are passed over
    QCOMPARE(m.hotkeys.value(0), (MusicBeeHotkey { 113, s(u"EditClickedColumn"), false }));
    QCOMPARE(m.hotkeys.value(2), (MusicBeeHotkey { 131156, s(u"FileNewTab"), false }));
    QCOMPARE(m.hotkeys.value(9), (MusicBeeHotkey { 176, s(u"MultiMediaNext"), true }));
    QCOMPARE(m.hotkeys.value(16), (MusicBeeHotkey { 131157, s(u"PlaybackPreviousTrack"), true }));

    QCOMPARE(m.namingRules.keys(),
        QStringList({ s(u"AutoTagFromFilenameTemplate"), s(u"BurnFilenameTemplate"), s(u"OrganisationDefaultFilenameRules") }));
    QCOMPARE(m.namingRules.value(s(u"OrganisationDefaultFilenameRules")),
        QList<MusicBeeNamingPart>({ part(31, u"\\"), part(30, u"\\"), part(53, u" "), part(65, u"") }));
    QCOMPARE(m.namingRules.value(s(u"BurnFilenameTemplate")),
        QList<MusicBeeNamingPart>({ part(0, u"Music\\"), part(0, u"-", u"$Pad(<Rank>,3)"), part(32, u"-"), part(65, u"") }));
}

void SettingsImportTest::readingOddFiles()
{
    // SI-READ: what is and is not a settings file.
    for (const QByteArray &bad : { QByteArray(), QByteArray("hello"), QByteArray("<?xml version=\"1.0\"?>\r\n<Other><A>1</A></Other>\r\n"),
             QByteArray("<?xml version=\"1.0\"?>\r\n<ApplicationSettings>\r\n<A>1</A>\r\n"), // never closed
             fixture().left(fixture().size() / 2) }) {
        const MusicBeeSettings m = readMusicBeeSettings(bad);
        QVERIFY2(!m.ok && !m.error.isEmpty(), bad.left(60).constData());
        QCOMPARE(planSettingsImport(m).size(), 0);
    }
    // A byte order mark, LF line ends, no declaration, nothing in it.
    MusicBeeSettings m = readMusicBeeSettings(QByteArray("\xEF\xBB\xBF<ApplicationSettings>\n<A>1</A>\n<B> two words </B>\n</ApplicationSettings>\n"));
    QVERIFY2(m.ok, qPrintable(m.error));
    QCOMPARE(m.values.value(s(u"A")), s(u"1"));
    QCOMPARE(m.values.value(s(u"B")), s(u" two words "));
    QCOMPARE(m.hotkeys.size(), 0);
    QCOMPARE(m.namingRules.size(), 0);
    QCOMPARE(planSettingsImport(m).size(), 0);
    m = readMusicBeeSettings(wrapped(""));
    QVERIFY(m.ok && m.values.isEmpty());
    // IsGlobal is true only for "true"; a rule with no Field has field 0; a list that mixes is no naming rule.
    m = readMusicBeeSettings(wrapped("<HotKeys>\r\n <KeyCommandPair>\r\n  <Key>65</Key>\r\n  <Command>X</Command>\r\n  <IsGlobal>True</IsGlobal>\r\n </KeyCommandPair>\r\n"
                                     " <KeyCommandPair>\r\n  <Key> 66 </Key>\r\n  <Command>Y</Command>\r\n </KeyCommandPair>\r\n</HotKeys>\r\n"
                                     "<R>\r\n <NamingRule>\r\n  <Text>only</Text>\r\n </NamingRule>\r\n <NamingRule>\r\n  <Field>x</Field>\r\n </NamingRule>\r\n</R>\r\n"
                                     "<Mixed>\r\n <NamingRule>\r\n  <Field>30</Field>\r\n </NamingRule>\r\n <Other>1</Other>\r\n</Mixed>\r\n"));
    QVERIFY(m.ok);
    QCOMPARE(m.hotkeys, QList<MusicBeeHotkey>({ { 65, s(u"X"), false }, { 66, s(u"Y"), false } }));
    QCOMPARE(m.namingRules.keys(), QStringList({ s(u"R") }));
    QCOMPARE(m.namingRules.value(s(u"R")), QList<MusicBeeNamingPart>({ part(0, u"only"), part(0, u"") }));
}

void SettingsImportTest::keys()
{
    // SI-KEY. The numbers are those seen in a real file.
    const QList<std::pair<int, const char16_t *>> seen {
        { 113, u"F2" }, { 116, u"F5" }, { 131156, u"Ctrl+T" }, { 131137, u"Ctrl+A" }, { 65549, u"Shift+Return" },
        { 262157, u"Alt+Return" }, { 196621, u"Ctrl+Shift+Return" }, { 131085, u"Ctrl+Return" }, { 45, u"Ins" },
        { 393254, u"Ctrl+Alt+Up" }, { 393256, u"Ctrl+Alt+Down" }, { 262253, u"Alt+-" }, { 262251, u"Alt++" },
        { 262192, u"Alt+0" }, { 262333, u"Alt+-" }, { 393405, u"Ctrl+Alt+-" }, { 122, u"F11" }, { 176, u"Media Next" },
        { 177, u"Media Previous" }, { 178, u"Media Stop" }, { 179, u"Media Play" }, { 173, u"Volume Mute" },
        { 458752 + 90, u"Ctrl+Alt+Shift+Z" }, { 135, u"F24" }, { 32, u"Space" }, { 27, u"Esc" }, { 46, u"Del" },
        { 33, u"PgUp" }, { 34, u"PgDown" }, { 8, u"Backspace" }, { 9, u"Tab" }, { 220, u"\\" }, { 222, u"'" }, { 192, u"`" },
    };
    for (const auto &[stored, want] : seen)
        QCOMPARE(hotkeyText(stored), s(want));
    // No name: unknown codes, modifiers alone, bits that are not modifiers, nonsense.
    for (const int stored : { 0, -1, 12, 16, 17, 18, 91, 96, 105, 108, 136, 255, 65536, 131072, 262144, 131072 + 12, 524288 + 65, 1048576 + 65, 65535 })
        QVERIFY2(hotkeyText(stored).isEmpty(), qPrintable(QString::number(stored)));
    // Every text it gives is one Qt reads back unchanged, with and without modifiers.
    int named = 0;
    for (int code = 1; code < 256; ++code) {
        for (const int modifiers : { 0, 65536, 131072, 262144, 458752 }) {
            const QString text = hotkeyText(code + modifiers);
            if (text.isEmpty()) {
                QVERIFY2(hotkeyText(code).isEmpty(), qPrintable(QString::number(code))); // a modifier does not change whether a key has a name
                continue;
            }
            const QKeySequence sequence(text, QKeySequence::PortableText);
            QCOMPARE(sequence.count(), 1);
            QCOMPARE(sequence.toString(QKeySequence::PortableText), text);
            if (modifiers == 0)
                ++named;
        }
    }
    QCOMPARE(named, 26 + 10 + 24 + 5 + 4 + 4 + 2 + 4 + 6 + 5 + 3 + 4); // the table of SI-KEY, line by line
}

void SettingsImportTest::templates()
{
    // SI-TEMPLATE
    QCOMPARE(templateFromNamingRule({ part(31, u"\\"), part(30, u"\\"), part(53, u" "), part(65, u"") }),
        s(u"<Album Artist>\\<Album>\\<Disc-Track#> <Title>"));
    QCOMPARE(templateFromNamingRule({ part(31, u"\\"), part(30, u"\\"), part(86, u" - "), part(65, u"") }),
        s(u"<Album Artist>\\<Album>\\<Track#> - <Title>"));
    QCOMPARE(templateFromNamingRule({ part(0, u"Music\\"), part(32, u" - "), part(65, u".x") }), s(u"Music\\<Artist> - <Title>.x"));
    QCOMPARE(templateFromNamingRule({ part(65, u"") }), s(u"<Title>"));
    QCOMPARE(templateFromNamingRule({ part(0, u"fixed") }), s(u"fixed"));
    // Nothing is carried over when a part is not understood.
    QCOMPARE(templateFromNamingRule({ }), QString());
    QCOMPARE(templateFromNamingRule({ part(31, u"\\"), part(39, u" "), part(65, u"") }), QString()); // a field that is not known
    QCOMPARE(templateFromNamingRule({ part(31, u"\\"), part(-1, u"") }), QString());
    QCOMPARE(templateFromNamingRule({ part(0, u"-", u"$Pad(<Rank>,3)"), part(65, u"") }), QString()); // a formula
    for (const char16_t *text : { u"a<b", u"a>b", u"$If(x)" })
        QCOMPARE(templateFromNamingRule({ part(65, text) }), QString());
}

void SettingsImportTest::plan()
{
    // SI-PLAN, line by line for the fixture.
    const QList<SettingsImportLine> lines = planSettingsImport(readMusicBeeSettings(fixture()));
    const QList<SettingsImportLine> want {
        imported(u"hotkey EditClickedColumn", u"hotkeys/edit.editField", u"F2"),
        imported(u"hotkey GeneralReload", u"hotkeys/view.refresh", u"F5"),
        skipped(u"hotkey FileNewTab", u"linuxBee has no such command"),
        imported(u"hotkey EditSelectAll", u"hotkeys/edit.selectAll", u"Ctrl+A"),
        imported(u"hotkey EditProperties", u"hotkeys/file.editTags", u"Shift+Return"),
        imported(u"hotkey EditCustomSearch", u"hotkeys/edit.find", u"Ctrl+F"),
        imported(u"hotkey PlaybackPlayNow", u"hotkeys/track.playNow", u"Alt+Return"),
        imported(u"hotkey PlaybackPlayNext", u"hotkeys/track.queueNext", u"Ctrl+Shift+Return"),
        imported(u"hotkey PlaybackPlayLast", u"hotkeys/track.queueLast", u"Ctrl+Return"),
        skipped(u"hotkey MultiMediaNext", u"linuxBee has no such command"), // that, before its being global
        imported(u"hotkey PlaybackPlayPause", u"hotkeys/controls.playPause", u"Ctrl+P"),
        skipped(u"hotkey PlaybackStop", u"the key is already taken by controls.playPause"),
        imported(u"hotkey PlaybackVolumeUp", u"hotkeys/controls.volumeUp", u"Ctrl+Alt+Up"),
        skipped(u"hotkey RatingSelectedFilesTickDown", u"linuxBee has no such command"),
        skipped(u"hotkey PlaybackNextTrack", u"the key has no name here"),
        skipped(u"hotkey EditSelectAll", u"already set by an earlier line"),
        skipped(u"hotkey PlaybackPreviousTrack", u"global hotkeys are not supported"),
        imported(u"organise template", u"organise/template", u"<Album Artist>\\<Album>\\<Disc-Track#> <Title>"),
    };
    QCOMPARE(lines.size(), want.size());
    for (int i = 0; i < want.size(); ++i) {
        const SettingsImportLine got = lines.value(i);
        QVERIFY2(got == want.at(i),
            qPrintable(QStringLiteral("line %1: [%2] [%3] [%4] [%5]").arg(i).arg(got.what, got.key, got.value, got.note)));
        QCOMPARE(got.imported(), !want.at(i).key.isEmpty());
    }
    // A line that was not imported does not hold its command or its key against a later one.
    MusicBeeSettings m;
    m.ok = true;
    m.hotkeys = { { 131072 + 12, s(u"EditCopy"), false }, { 67, s(u"EditCopy"), true }, { 131139, s(u"EditCopy"), false },
        { 131139, s(u"NoSuchThing"), false }, { 131139, s(u"EditPaste"), false } };
    const QList<SettingsImportLine> second = planSettingsImport(m);
    QCOMPARE(second.size(), 5); // no naming rule in it: no line for one
    QCOMPARE(second.value(0).note, s(u"the key has no name here"));
    QCOMPARE(second.value(1).note, s(u"global hotkeys are not supported"));
    QCOMPARE(second.value(2), imported(u"hotkey EditCopy", u"hotkeys/edit.copyTags", u"Ctrl+C"));
    QCOMPARE(second.value(3).note, s(u"linuxBee has no such command"));
    QCOMPARE(second.value(4).note, s(u"the key is already taken by edit.copyTags"));
    // A naming rule that cannot be carried over is said so; other rules get no line.
    m.hotkeys.clear();
    m.namingRules.insert(s(u"OrganisationDefaultFilenameRules"), { part(39, u"") });
    m.namingRules.insert(s(u"RipFilenameTemplate"), { part(65, u"") });
    QCOMPARE(planSettingsImport(m), QList<SettingsImportLine>({ skipped(u"organise template", u"a part of the rule cannot be carried over") }));
    m.ok = false;
    QCOMPARE(planSettingsImport(m).size(), 0);
}

void SettingsImportTest::commandsExist()
{
    // SI-PLAN: every command the table names is one linuxBee has.
    const QStringList names { s(u"EditClickedColumn"), s(u"EditSelectAll"), s(u"EditCopy"), s(u"EditPaste"), s(u"EditProperties"),
        s(u"EditCustomSearch"), s(u"EditPreferences"), s(u"GeneralReload"), s(u"FileScanNewMedia"), s(u"PlaybackPlayNow"),
        s(u"PlaybackPlayNext"), s(u"PlaybackPlayLast"), s(u"PlaybackNextTrack"), s(u"PlaybackPreviousTrack"),
        s(u"PlaybackPlayPause"), s(u"PlaybackStop"), s(u"PlaybackVolumeUp"), s(u"PlaybackVolumeDown"), s(u"PlaybackVolumeMute"),
        s(u"ToolsAutoOrganise"), s(u"ToolsAnalyseVolume") };
    MusicBeeSettings m;
    m.ok = true;
    for (int i = 0; i < names.size(); ++i)
        m.hotkeys.append({ 131072 + 65 + i, names.at(i), false }); // Ctrl+A, Ctrl+B, ...
    const QList<SettingsImportLine> lines = planSettingsImport(m);
    QCOMPARE(lines.size(), names.size());
    ui::ActionRegistry registry;
    QStringList ids;
    for (const SettingsImportLine &line : lines) {
        QVERIFY2(line.imported(), qPrintable(line.what + QLatin1Char(' ') + line.note));
        QVERIFY(line.key.startsWith(s(u"hotkeys/")));
        const QString id = line.key.mid(8);
        QVERIFY2(registry.ids().contains(id), qPrintable(id));
        QVERIFY2(!ids.contains(id), qPrintable(id)); // two names never lead to one command
        ids.append(id);
    }
    QCOMPARE(ids.value(4), s(u"file.editTags"));
    QCOMPARE(ids.value(19), s(u"file.organise"));
    // And what is stored is what the registry reads at the next start.
    QTemporaryDir dir;
    QSettings settings(dir.path() + s(u"/linuxbee.ini"), QSettings::IniFormat);
    QCOMPARE(applySettingsImport(lines, settings), int(names.size()));
    settings.sync();
    QSettings later(settings.fileName(), QSettings::IniFormat);
    registry.load(later);
    QCOMPARE(registry.shortcut(s(u"file.editTags")).toString(QKeySequence::PortableText), s(u"Ctrl+E"));
    QCOMPARE(registry.shortcut(s(u"tools.analyseVolume")).toString(QKeySequence::PortableText), s(u"Ctrl+U"));
}

void SettingsImportTest::applying()
{
    // SI-APPLY
    QTemporaryDir dir;
    QSettings settings(dir.path() + s(u"/linuxbee.ini"), QSettings::IniFormat);
    settings.setValue(s(u"hotkeys/controls.stop"), s(u"Ctrl+S"));
    settings.setValue(s(u"organise/copy"), s(u"true"));
    settings.setValue(s(u"organise/template"), s(u"<Title>"));
    const QList<SettingsImportLine> lines = planSettingsImport(readMusicBeeSettings(fixture()));
    QCOMPARE(applySettingsImport(lines, settings), 11);
    settings.sync();
    QSettings later(settings.fileName(), QSettings::IniFormat);
    QCOMPARE(later.value(s(u"hotkeys/edit.editField")).toString(), s(u"F2"));
    QCOMPARE(later.value(s(u"hotkeys/track.queueNext")).toString(), s(u"Ctrl+Shift+Return"));
    QCOMPARE(later.value(s(u"hotkeys/controls.playPause")).toString(), s(u"Ctrl+P"));
    QCOMPARE(later.value(s(u"organise/template")).toString(), s(u"<Album Artist>\\<Album>\\<Disc-Track#> <Title>")); // replaced
    QCOMPARE(later.value(s(u"hotkeys/controls.stop")).toString(), s(u"Ctrl+S")); // not in the plan: left alone
    QCOMPARE(later.value(s(u"organise/copy")).toString(), s(u"true"));
    later.beginGroup(s(u"hotkeys"));
    QCOMPARE(later.childKeys().size(), 11); // ten imported and the one that was there
    later.endGroup();
    QCOMPARE(later.allKeys().size(), 13);
    // Lines that were not imported write nothing, whatever they hold.
    QSettings empty(dir.path() + s(u"/empty.ini"), QSettings::IniFormat);
    QCOMPARE(applySettingsImport({ skipped(u"hotkey X", u"no"), { s(u"odd"), QString(), s(u"value"), QString() } }, empty), 0);
    QCOMPARE(empty.allKeys().size(), 0);
    QCOMPARE(applySettingsImport({ }, empty), 0);
}

QTEST_MAIN(SettingsImportTest)
#include "tst_ui_settingsimport.moc"
