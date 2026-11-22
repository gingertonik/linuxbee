#pragma once
#include <QByteArray>
#include <QMap>
#include <QString>
#include <QStringList>

#include <optional>

// Reading a MusicBee skin (a plain .xml one) and working out the colours
// linuxBee uses from it. Contract: docs/spec/skins.md (SK-READ, SK-LAYER,
// SK-COLOURS, SK-SHEET). Pure: it is given bytes and opens no file.
namespace linuxbee {

struct SkinColour {
    int r = 0;
    int g = 0;
    int b = 0;
    int a = 255;

    bool operator==(const SkinColour &other) const = default;
};

struct SkinElement {
    std::optional<SkinColour> bg;
    std::optional<SkinColour> bg2;
    std::optional<SkinColour> fg;
    std::optional<SkinColour> fg2;
    std::optional<SkinColour> bdr;
    QString text; // what stands between the tags, trimmed

    bool operator==(const SkinElement &other) const = default;
};

struct Skin {
    bool ok = false;
    QString error; // set when !ok
    QString dependsOn; // the file the skin says it is laid over; empty: none
    QMap<QString, SkinColour> variables;
    QMap<QString, SkinElement> elements; // by id
    QStringList problems; // values that could not be read, one line each
};

// SK-READ
Skin readSkin(const QByteArray &bytes);

// SK-LAYER: `top` laid over `base`.
Skin layeredSkin(const Skin &base, const Skin &top);

// SK-COLOURS: linuxBee's colour names the skin has a value for.
QMap<QString, SkinColour> skinColours(const Skin &skin);

// "r,g,b", or "a,r,g,b" when a is not 255 (alpha first, SK-1).
QString skinColourText(const SkinColour &colour);

// SK-SHEET: the Qt style sheet for those colours. Empty for none.
QString skinStyleSheet(const QMap<QString, SkinColour> &colours);

}
