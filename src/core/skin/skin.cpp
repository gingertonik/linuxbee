#include "skin/skin.h"

#include <QRegularExpression>
#include <QXmlStreamReader>

namespace linuxbee {

namespace {

QString quoted(const QString &text)
{
    return QLatin1Char('"') + text + QLatin1Char('"');
}

// SK-READ, "A colour": three or four whole numbers from 0 to 255 separated by
// commas, spaces allowed around each. The fourth is the alpha.
std::optional<SkinColour> parseColour(const QString &text)
{
    const QStringList parts = text.split(QLatin1Char(','));
    if (parts.size() != 3 && parts.size() != 4)
        return std::nullopt;
    int numbers[4] = { 0, 0, 0, 255 };
    for (qsizetype i = 0; i < parts.size(); ++i) {
        const QString part = parts.at(i).trimmed();
        if (part.isEmpty())
            return std::nullopt;
        for (const QChar digit : part) {
            if (digit.unicode() < u'0' || digit.unicode() > u'9')
                return std::nullopt;
        }
        bool isNumber = false;
        const int value = part.toInt(&isNumber);
        if (!isNumber || value > 255)
            return std::nullopt;
        numbers[i] = value;
    }
    return SkinColour { numbers[0], numbers[1], numbers[2], numbers[3] };
}

// SK-READ, `variables`: one line is NAME = "VALUE".
const QRegularExpression &variableLine()
{
    static const QRegularExpression pattern(
        QStringLiteral(R"re(^[ \t]*([A-Za-z0-9_-]+)[ \t]*=[ \t]*"([^"]*)"[ \t]*$)re"));
    return pattern;
}

void readVariableLines(const QString &text, Skin *skin)
{
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (QString line : lines) {
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        const QRegularExpressionMatch match = variableLine().match(line);
        if (!match.hasMatch())
            continue; // other lines mean nothing
        const QString name = match.captured(1);
        const QString value = match.captured(2);
        const std::optional<SkinColour> colour = parseColour(value);
        if (colour)
            skin->variables.insert(name, *colour);
        else
            skin->problems.append(QStringLiteral("variable ") + name
                + QStringLiteral(": cannot read ") + quoted(value));
    }
}

// An element's colour attribute: a colour, or the name of a variable of the
// file. An empty value is no value; anything else that is neither adds a
// problem.
std::optional<SkinColour> attributeColour(const Skin &skin, const QString &id,
    const QString &attribute, const QString &value, QStringList *problems)
{
    const QString text = value.trimmed();
    if (text.isEmpty())
        return std::nullopt;
    if (const std::optional<SkinColour> colour = parseColour(text))
        return colour;
    const auto variable = skin.variables.constFind(text);
    if (variable != skin.variables.cend())
        return variable.value();
    problems->append(
        id + QLatin1Char(' ') + attribute + QStringLiteral(": cannot read ") + quoted(value));
    return std::nullopt;
}

// SK-READ, "An id met again": each colour the later one has replaces the
// earlier one's, the others stay; its text replaces the earlier text when it
// is not empty. SK-LAYER joins an id that both skins have the same way.
void joinElement(SkinElement *into, const SkinElement &later)
{
    if (later.bg)
        into->bg = later.bg;
    if (later.bg2)
        into->bg2 = later.bg2;
    if (later.fg)
        into->fg = later.fg;
    if (later.fg2)
        into->fg2 = later.fg2;
    if (later.bdr)
        into->bdr = later.bdr;
    if (!later.text.isEmpty())
        into->text = later.text;
}

Skin notASkin(const QString &reason)
{
    Skin skin;
    skin.error = reason;
    return skin;
}

}

Skin readSkin(const QByteArray &bytes)
{
    Skin skin;

    // First pass: the root, and the variables wherever their block stands.
    {
        QXmlStreamReader xml(bytes);
        bool sawRoot = false;
        while (!xml.atEnd()) {
            xml.readNext();
            if (!xml.isStartElement())
                continue;
            if (!sawRoot) {
                sawRoot = true;
                if (xml.name() != QLatin1String("root"))
                    return notASkin(QStringLiteral("the file has no root element"));
                skin.dependsOn
                    = xml.attributes().value(QLatin1String("dependsOn")).toString().trimmed();
            } else if (xml.name() == QLatin1String("variables")) {
                readVariableLines(
                    xml.readElementText(QXmlStreamReader::IncludeChildElements), &skin);
            }
        }
        if (xml.hasError())
            return notASkin(
                QStringLiteral("line %1: %2").arg(xml.lineNumber()).arg(xml.errorString()));
        if (!sawRoot)
            return notASkin(QStringLiteral("the file has no root element"));
    }

    // Second pass: the elements, now that every variable is known. The
    // problems of the variables come first, then those of the elements.
    QXmlStreamReader xml(bytes);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement() || xml.name() != QLatin1String("element"))
            continue;
        const QXmlStreamAttributes attributes = xml.attributes();
        const QString id = attributes.value(QLatin1String("id")).toString();
        if (id.isEmpty())
            continue;
        SkinElement element;
        element.bg = attributeColour(skin, id, QStringLiteral("bg"),
            attributes.value(QLatin1String("bg")).toString(), &skin.problems);
        element.bg2 = attributeColour(skin, id, QStringLiteral("bg2"),
            attributes.value(QLatin1String("bg2")).toString(), &skin.problems);
        element.fg = attributeColour(skin, id, QStringLiteral("fg"),
            attributes.value(QLatin1String("fg")).toString(), &skin.problems);
        element.fg2 = attributeColour(skin, id, QStringLiteral("fg2"),
            attributes.value(QLatin1String("fg2")).toString(), &skin.problems);
        element.bdr = attributeColour(skin, id, QStringLiteral("bdr"),
            attributes.value(QLatin1String("bdr")).toString(), &skin.problems);
        element.text = xml.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
        joinElement(&skin.elements[id], element);
    }

    skin.ok = true;
    return skin;
}

Skin layeredSkin(const Skin &base, const Skin &top)
{
    if (!top.ok || !base.ok)
        return top;
    Skin both;
    both.ok = true;
    both.dependsOn = base.dependsOn;
    both.variables = base.variables;
    for (auto it = top.variables.cbegin(); it != top.variables.cend(); ++it)
        both.variables.insert(it.key(), it.value());
    both.elements = base.elements;
    for (auto it = top.elements.cbegin(); it != top.elements.cend(); ++it)
        joinElement(&both.elements[it.key()], it.value());
    both.problems = base.problems + top.problems;
    return both;
}

namespace {

// SK-COLOURS: where one of linuxBee's colour names comes from.
struct Source {
    QLatin1String element;
    bool background; // the element's bg; otherwise its fg
};

struct ColourRule {
    QLatin1String name;
    QList<Source> sources; // tried in this order
};

Source sourceBg(const char *element)
{
    return { QLatin1String(element), true };
}

Source sourceFg(const char *element)
{
    return { QLatin1String(element), false };
}

ColourRule colourRule(const char *name, const QList<Source> &sources)
{
    return { QLatin1String(name), sources };
}

// The table of SK-COLOURS, in the spec's order.
const QList<ColourRule> &colourRules()
{
    static const QList<ColourRule> rules = {
        colourRule("window", { sourceBg("WindowBackColour"), sourceBg("ApplicationBackColour") }),
        colourRule("windowText", { sourceFg("Panel.Body.Default") }),
        colourRule("base", { sourceBg("Panel.Body.Default") }),
        colourRule("text", { sourceFg("Panel.Body.Default") }),
        colourRule("alternateBase", { sourceBg("Content[TrackDetail].ListAlternating.Default") }),
        colourRule("dimText", { sourceFg("Panel.Body.Lowlight") }),
        colourRule("highlight", { sourceBg("Panel.Body.Highlight") }),
        colourRule("highlightText", { sourceFg("Panel.Body.Highlight") }),
        colourRule("button", { sourceBg("Controls.Button.Default") }),
        colourRule("buttonText", { sourceFg("Controls.Button.Default") }),
        colourRule(
            "header", { sourceBg("Panel.Header.Default"), sourceBg("Panel.ChildHeader.Default") }),
        colourRule("headerText",
            { sourceFg("Panel.Header.Default"), sourceFg("Panel.ChildHeader.Default") }),
        colourRule("menuBar", { sourceBg("MenuBar.Default") }),
        colourRule("menuBarText", { sourceFg("MenuBar.Default") }),
        colourRule("menu", { sourceBg("Menu.DropDown.Default") }),
        colourRule("menuText", { sourceFg("Menu.DropDown.Default") }),
        colourRule("menuHighlight", { sourceBg("Menu.DropDown.Highlight") }),
        colourRule("menuHighlightText",
            { sourceFg("Menu.DropDown.HighlightText"), sourceFg("Panel.Body.Highlight") }),
        colourRule("statusBar", { sourceBg("Panel.StatusBar.Default") }),
        colourRule("statusBarText", { sourceFg("Panel.StatusBar.Default") }),
        colourRule("player",
            { sourceBg("Player.Background"), sourceBg("PlayerFlat.Background"),
                sourceBg("PlayerSidebar.Background") }),
        colourRule("playerText",
            { sourceFg("Player.Background"), sourceFg("PlayerFlat.Background"),
                sourceFg("PlayerSidebar.Background") }),
        colourRule("tab", { sourceBg("Controls.TabHeader.Default") }),
        colourRule("tabText", { sourceFg("Controls.TabHeader.Default") }),
        colourRule("tabSelected", { sourceBg("Controls.TabHeader.Highlight") }),
        colourRule("tabSelectedText", { sourceFg("Controls.TabHeader.Highlight") }),
    };
    return rules;
}

// SK-SHEET: one line of the sheet, written when both colour names are there.
// %1 is the first colour and %2 the second.
struct SheetLine {
    QLatin1String first;
    QLatin1String second;
    QLatin1String pattern;
};

// The table of SK-SHEET, in the spec's order.
const QList<SheetLine> &sheetLines()
{
    static const QList<SheetLine> lines = {
        { QLatin1String("header"), QLatin1String("headerText"),
            QLatin1String("QHeaderView::section { background-color: %1; color: %2; }") },
        { QLatin1String("menuBar"), QLatin1String("menuBarText"),
            QLatin1String("QMenuBar { background-color: %1; color: %2; }") },
        { QLatin1String("menu"), QLatin1String("menuText"),
            QLatin1String("QMenu { background-color: %1; color: %2; }") },
        { QLatin1String("menuHighlight"), QLatin1String("menuHighlightText"),
            QLatin1String("QMenu::item:selected { background-color: %1; color: %2; }") },
        { QLatin1String("statusBar"), QLatin1String("statusBarText"),
            QLatin1String("QStatusBar { background-color: %1; color: %2; }") },
        { QLatin1String("player"), QLatin1String("playerText"),
            QLatin1String("#playerBar { background-color: %1; color: %2; } "
                          "#playerBar QLabel { color: %2; }") },
        { QLatin1String("tab"), QLatin1String("tabText"),
            QLatin1String("QTabBar::tab { background-color: %1; color: %2; padding: 4px 10px; }") },
        { QLatin1String("tabSelected"), QLatin1String("tabSelectedText"),
            QLatin1String("QTabBar::tab:selected { background-color: %1; color: %2; }") },
    };
    return lines;
}

// A colour as the sheet writes it: rgb(r,g,b), or rgba(r,g,b,a) with an alpha.
QString sheetColour(const SkinColour &colour)
{
    const QString prefix = colour.a == 255 ? QStringLiteral("rgb(") : QStringLiteral("rgba(");
    return prefix + skinColourText(colour) + QLatin1Char(')');
}

}

QMap<QString, SkinColour> skinColours(const Skin &skin)
{
    QMap<QString, SkinColour> colours;
    if (!skin.ok)
        return colours;
    for (const ColourRule &rule : colourRules()) {
        for (const Source &source : rule.sources) {
            const auto element = skin.elements.constFind(QString(source.element));
            if (element == skin.elements.cend())
                continue;
            const std::optional<SkinColour> &colour = source.background ? element->bg : element->fg;
            if (!colour)
                continue;
            colours.insert(QString(rule.name), *colour);
            break; // the first source the skin has
        }
    }
    return colours;
}

QString skinColourText(const SkinColour &colour)
{
    QString text = QStringLiteral("%1,%2,%3").arg(colour.r).arg(colour.g).arg(colour.b);
    if (colour.a != 255)
        text += QStringLiteral(",%1").arg(colour.a);
    return text;
}

QString skinStyleSheet(const QMap<QString, SkinColour> &colours)
{
    QStringList lines;
    for (const SheetLine &line : sheetLines()) {
        const auto first = colours.constFind(QString(line.first));
        const auto second = colours.constFind(QString(line.second));
        if (first == colours.cend() || second == colours.cend())
            continue;
        lines.append(
            QString(line.pattern).arg(sheetColour(first.value()), sheetColour(second.value())));
    }
    return lines.join(QLatin1Char('\n'));
}

}
