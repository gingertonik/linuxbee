#include "settingsimport/settingsimport.h"

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

QString hotkeyText(int storedKey)
{
    Q_UNUSED(storedKey);
    return QStringLiteral("?");
}

QString templateFromNamingRule(const QList<MusicBeeNamingPart> &parts)
{
    Q_UNUSED(parts);
    return QStringLiteral("?");
}

QList<SettingsImportLine> planSettingsImport(const MusicBeeSettings &settings)
{
    Q_UNUSED(settings);
    return { };
}

int applySettingsImport(const QList<SettingsImportLine> &plan, QSettings &target)
{
    Q_UNUSED(plan);
    Q_UNUSED(target);
    return -1;
}

}
