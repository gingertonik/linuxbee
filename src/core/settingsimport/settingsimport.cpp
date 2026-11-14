#include "settingsimport/settingsimport.h"

#include <QSet>
#include <QSettings>
#include <QXmlStreamReader>

#include <utility>
#include <vector>

// Stub. Plan 22 replaces every body in this file.
namespace linuxbee {

namespace {

// One element of the settings file as SI-READ reads it: its name, the text
// directly inside it, and its child elements in order.
struct XmlNode {
    QString name;
    QString text;
    std::vector<XmlNode> children;
};

// SI-READ: a byte below U+0020 other than tab, CR and LF is read as a space.
// UTF-8 never uses a byte below 0x20 inside a character, so no character is split.
QByteArray withControlsAsSpaces(QByteArray bytes)
{
    for (char &c : bytes) {
        const auto b = static_cast<unsigned char>(c);
        if (b < 0x20 && b != '\t' && b != '\r' && b != '\n')
            c = ' ';
    }
    return bytes;
}

// Reads the document up to the end of its root element. On failure, `error` says why.
bool readRoot(const QByteArray &bytes, XmlNode &root, QString &error)
{
    QXmlStreamReader xml(bytes);
    xml.setNamespaceProcessing(false);
    std::vector<XmlNode> open; // elements not yet closed, innermost last
    bool started = false;
    bool closed = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            started = true;
            XmlNode node;
            node.name = xml.name().toString();
            open.push_back(std::move(node));
        } else if (xml.isCharacters() && !open.empty()) {
            open.back().text.append(xml.text().toString());
        } else if (xml.isEndElement() && !open.empty()) {
            XmlNode done = std::move(open.back());
            open.pop_back();
            if (open.empty()) {
                root = std::move(done);
                closed = true;
                break;
            }
            open.back().children.push_back(std::move(done));
        }
    }
    if (closed)
        return true;
    if (xml.hasError())
        error = QStringLiteral("%1 (line %2)").arg(xml.errorString()).arg(xml.lineNumber());
    else if (started)
        error = QStringLiteral("the file ends before the ApplicationSettings element closes");
    else
        error = QStringLiteral("no ApplicationSettings root element");
    return false;
}

// The text of the last child element of `node` called `name`; empty when there is none.
QString childText(const XmlNode &node, const QString &name)
{
    QString text;
    for (const XmlNode &child : node.children) {
        if (child.name == name)
            text = child.text;
    }
    return text;
}

// SI-READ: each KeyCommandPair with a whole-number Key and a non-empty Command.
void addHotkeys(const XmlNode &hotKeys, QList<MusicBeeHotkey> &out)
{
    for (const XmlNode &pair : hotKeys.children) {
        if (pair.name != QStringLiteral("KeyCommandPair"))
            continue;
        bool keyIsNumber = false;
        const int key = childText(pair, QStringLiteral("Key")).trimmed().toInt(&keyIsNumber);
        const QString command = childText(pair, QStringLiteral("Command"));
        if (!keyIsNumber || command.isEmpty())
            continue;
        MusicBeeHotkey hotkey;
        hotkey.key = key;
        hotkey.command = command;
        hotkey.global = childText(pair, QStringLiteral("IsGlobal")) == QStringLiteral("true");
        out.append(hotkey);
    }
}

// SI-READ: a list is a naming rule when it holds NamingRule elements and nothing else.
bool isNamingRuleList(const XmlNode &node)
{
    if (node.children.empty())
        return false;
    for (const XmlNode &child : node.children) {
        if (child.name != QStringLiteral("NamingRule"))
            return false;
    }
    return true;
}

// SI-READ: the parts of a naming rule, in order. Field is 0 when missing or not a number.
QList<MusicBeeNamingPart> namingParts(const XmlNode &list)
{
    QList<MusicBeeNamingPart> parts;
    for (const XmlNode &rule : list.children) {
        bool fieldIsNumber = false;
        MusicBeeNamingPart part;
        part.field = childText(rule, QStringLiteral("Field")).trimmed().toInt(&fieldIsNumber);
        if (!fieldIsNumber)
            part.field = 0;
        part.text = childText(rule, QStringLiteral("Text"));
        part.function = childText(rule, QStringLiteral("Func"));
        parts.append(part);
    }
    return parts;
}

} // namespace

MusicBeeSettings readMusicBeeSettings(const QByteArray &bytes)
{
    MusicBeeSettings settings;
    QByteArray text = bytes;
    if (text.startsWith("\xEF\xBB\xBF"))
        text.remove(0, 3);
    XmlNode root;
    if (!readRoot(withControlsAsSpaces(text), root, settings.error))
        return settings;
    if (root.name != QStringLiteral("ApplicationSettings")) {
        settings.error
            = QStringLiteral("not a MusicBee settings file: the root element is %1").arg(root.name);
        return settings;
    }
    settings.ok = true;
    for (const XmlNode &child : root.children) {
        if (child.children.empty())
            settings.values.insert(child.name, child.text); // a name met twice: the last counts
        else if (child.name == QStringLiteral("HotKeys"))
            addHotkeys(child, settings.hotkeys);
        else if (isNamingRuleList(child))
            settings.namingRules.insert(child.name, namingParts(child));
    }
    return settings;
}

namespace {

// SI-KEY: the modifier bits of a stored key.
constexpr int shiftBit = 65536;
constexpr int controlBit = 131072;
constexpr int altBit = 262144;

struct NamedKey {
    int code;
    QLatin1String name;
};

// SI-KEY: the key codes with a name that is not a letter, a digit or F1 to F24.
constexpr NamedKey namedKeys[] = {
    { 8, QLatin1String("Backspace") },
    { 9, QLatin1String("Tab") },
    { 13, QLatin1String("Return") },
    { 27, QLatin1String("Esc") },
    { 32, QLatin1String("Space") },
    { 33, QLatin1String("PgUp") },
    { 34, QLatin1String("PgDown") },
    { 35, QLatin1String("End") },
    { 36, QLatin1String("Home") },
    { 37, QLatin1String("Left") },
    { 38, QLatin1String("Up") },
    { 39, QLatin1String("Right") },
    { 40, QLatin1String("Down") },
    { 45, QLatin1String("Ins") },
    { 46, QLatin1String("Del") },
    { 106, QLatin1String("*") },
    { 107, QLatin1String("+") },
    { 109, QLatin1String("-") },
    { 111, QLatin1String("/") },
    { 173, QLatin1String("Volume Mute") },
    { 174, QLatin1String("Volume Down") },
    { 175, QLatin1String("Volume Up") },
    { 176, QLatin1String("Media Next") },
    { 177, QLatin1String("Media Previous") },
    { 178, QLatin1String("Media Stop") },
    { 179, QLatin1String("Media Play") },
    { 186, QLatin1String(";") },
    { 187, QLatin1String("=") },
    { 188, QLatin1String(",") },
    { 189, QLatin1String("-") },
    { 190, QLatin1String(".") },
    { 191, QLatin1String("/") },
    { 192, QLatin1String("`") },
    { 219, QLatin1String("[") },
    { 220, QLatin1String("\\") },
    { 221, QLatin1String("]") },
    { 222, QLatin1String("'") },
};

// SI-KEY: the name of a key code (the low 16 bits), or empty when it has none.
QString keyName(int code)
{
    if ((code >= 65 && code <= 90) || (code >= 48 && code <= 57)) // A to Z, 0 to 9
        return QString(QChar(static_cast<char16_t>(code)));
    if (code >= 112 && code <= 135) // F1 to F24
        return QStringLiteral("F%1").arg(code - 111);
    for (const NamedKey &key : namedKeys) {
        if (key.code == code)
            return key.name.toString();
    }
    return { };
}

// SI-TEMPLATE: the tag for each field that has one.
struct FieldTag {
    int field;
    QLatin1String tag;
};

constexpr FieldTag fieldTags[] = {
    { 30, QLatin1String("<Album>") },
    { 31, QLatin1String("<Album Artist>") },
    { 32, QLatin1String("<Artist>") },
    { 53, QLatin1String("<Disc-Track#>") },
    { 65, QLatin1String("<Title>") },
    { 86, QLatin1String("<Track#>") },
};

} // namespace

QString hotkeyText(int storedKey)
{
    // SI-KEY: a number below 1, or a bit above the low 16 that is not a modifier, has no text.
    constexpr int modifiers = shiftBit | controlBit | altBit;
    if (storedKey < 1 || (storedKey & ~(0xFFFF | modifiers)) != 0)
        return { };
    const QString name = keyName(storedKey & 0xFFFF);
    if (name.isEmpty())
        return { };
    QString text;
    if (storedKey & controlBit)
        text += QStringLiteral("Ctrl+");
    if (storedKey & altBit)
        text += QStringLiteral("Alt+");
    if (storedKey & shiftBit)
        text += QStringLiteral("Shift+");
    return text + name;
}

QString templateFromNamingRule(const QList<MusicBeeNamingPart> &parts)
{
    // SI-TEMPLATE: each part gives its field's tag, then its text as it is. Nothing is
    // carried over when a part has a formula, a field that is not in the table, or
    // text that holds <, > or $.
    if (parts.isEmpty())
        return { };
    QString text;
    for (const MusicBeeNamingPart &part : parts) {
        if (!part.function.isEmpty() || part.text.contains(QLatin1Char('<'))
            || part.text.contains(QLatin1Char('>')) || part.text.contains(QLatin1Char('$')))
            return { };
        QString tag;
        if (part.field != 0) {
            bool known = false;
            for (const FieldTag &entry : fieldTags) {
                if (entry.field == part.field) {
                    tag = entry.tag.toString();
                    known = true;
                }
            }
            if (!known)
                return { };
        }
        text += tag + part.text;
    }
    return text;
}

namespace {

// SI-PLAN: a MusicBee command name and the linuxBee command it becomes.
struct CommandMapping {
    QLatin1String musicBee;
    QLatin1String linuxBee;
};

constexpr CommandMapping commandMappings[] = {
    { QLatin1String("EditClickedColumn"), QLatin1String("edit.editField") },
    { QLatin1String("EditSelectAll"), QLatin1String("edit.selectAll") },
    { QLatin1String("EditCopy"), QLatin1String("edit.copyTags") },
    { QLatin1String("EditPaste"), QLatin1String("edit.pasteTags") },
    { QLatin1String("EditProperties"), QLatin1String("file.editTags") },
    { QLatin1String("EditCustomSearch"), QLatin1String("edit.find") },
    { QLatin1String("EditPreferences"), QLatin1String("edit.preferences") },
    { QLatin1String("GeneralReload"), QLatin1String("view.refresh") },
    { QLatin1String("FileScanNewMedia"), QLatin1String("file.scanFolders") },
    { QLatin1String("PlaybackPlayNow"), QLatin1String("track.playNow") },
    { QLatin1String("PlaybackPlayNext"), QLatin1String("track.queueNext") },
    { QLatin1String("PlaybackPlayLast"), QLatin1String("track.queueLast") },
    { QLatin1String("PlaybackNextTrack"), QLatin1String("controls.next") },
    { QLatin1String("PlaybackPreviousTrack"), QLatin1String("controls.previous") },
    { QLatin1String("PlaybackPlayPause"), QLatin1String("controls.playPause") },
    { QLatin1String("PlaybackStop"), QLatin1String("controls.stop") },
    { QLatin1String("PlaybackVolumeUp"), QLatin1String("controls.volumeUp") },
    { QLatin1String("PlaybackVolumeDown"), QLatin1String("controls.volumeDown") },
    { QLatin1String("PlaybackVolumeMute"), QLatin1String("controls.mute") },
    { QLatin1String("ToolsAutoOrganise"), QLatin1String("file.organise") },
    { QLatin1String("ToolsAnalyseVolume"), QLatin1String("tools.analyseVolume") },
};

// SI-PLAN: linuxBee's command for a MusicBee command name, or empty when there is none.
QString linuxBeeCommand(const QString &musicBee)
{
    for (const CommandMapping &mapping : commandMappings) {
        if (musicBee == mapping.musicBee)
            return mapping.linuxBee.toString();
    }
    return { };
}

} // namespace

QList<SettingsImportLine> planSettingsImport(const MusicBeeSettings &settings)
{
    QList<SettingsImportLine> lines;
    if (!settings.ok)
        return lines;

    // Only an imported line holds its command and its key against the lines after it.
    QSet<QString> importedCommands;
    QMap<QString, QString> keyOwner; // key text -> the linuxBee command it was imported for
    for (const MusicBeeHotkey &hotkey : settings.hotkeys) {
        SettingsImportLine line;
        line.what = QStringLiteral("hotkey ") + hotkey.command;
        const QString command = linuxBeeCommand(hotkey.command);
        const QString key = hotkeyText(hotkey.key);
        if (command.isEmpty())
            line.note = QStringLiteral("linuxBee has no such command");
        else if (hotkey.global)
            line.note = QStringLiteral("global hotkeys are not supported");
        else if (key.isEmpty())
            line.note = QStringLiteral("the key has no name here");
        else if (importedCommands.contains(command))
            line.note = QStringLiteral("already set by an earlier line");
        else if (keyOwner.contains(key))
            line.note = QStringLiteral("the key is already taken by %1").arg(keyOwner.value(key));
        else {
            line.key = QStringLiteral("hotkeys/") + command;
            line.value = key;
            importedCommands.insert(command);
            keyOwner.insert(key, command);
        }
        lines.append(line);
    }

    const QString organiseRule = QStringLiteral("OrganisationDefaultFilenameRules");
    if (settings.namingRules.contains(organiseRule)) {
        SettingsImportLine line;
        line.what = QStringLiteral("organise template");
        const QString tmpl = templateFromNamingRule(settings.namingRules.value(organiseRule));
        if (tmpl.isEmpty()) {
            line.note = QStringLiteral("a part of the rule cannot be carried over");
        } else {
            line.key = QStringLiteral("organise/template");
            line.value = tmpl;
        }
        lines.append(line);
    }
    return lines;
}

int applySettingsImport(const QList<SettingsImportLine> &plan, QSettings &target)
{
    int written = 0;
    for (const SettingsImportLine &line : plan) {
        if (!line.imported())
            continue;
        target.setValue(line.key, line.value);
        ++written;
    }
    return written;
}

}
