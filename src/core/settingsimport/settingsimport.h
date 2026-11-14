#pragma once
#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>

class QSettings;

// Reading a MusicBee settings file (MusicBee3Settings.ini) and carrying over
// what linuxBee has a place for. Contract: docs/spec/settings-import.md
// (SI-READ, SI-KEY, SI-TEMPLATE, SI-PLAN, SI-APPLY). Reads bytes it is given;
// only applySettingsImport writes, and only to the settings it is handed.
namespace linuxbee {

struct MusicBeeHotkey {
    int key = 0; // as stored: a key code plus modifier bits (SI-KEY)
    QString command; // MusicBee's name for the command
    bool global = false;

    bool operator==(const MusicBeeHotkey &other) const = default;
};

struct MusicBeeNamingPart {
    int field = 0; // 0: no field
    QString text; // literal text after the field
    QString function; // not empty: a formula linuxBee does not read

    bool operator==(const MusicBeeNamingPart &other) const = default;
};

struct MusicBeeSettings {
    bool ok = false;
    QString error; // set when !ok
    QMap<QString, QString> values; // SI-READ: simple top-level settings, by name
    QList<MusicBeeHotkey> hotkeys; // in file order
    QMap<QString, QList<MusicBeeNamingPart>> namingRules; // by the name of the setting
};

// SI-READ
MusicBeeSettings readMusicBeeSettings(const QByteArray &bytes);

// SI-KEY: the shortcut as Qt's portable text ("Ctrl+Shift+Return"), or empty
// when the stored number has no such text.
QString hotkeyText(int storedKey);

// SI-TEMPLATE: a naming rule as a linuxBee template, or empty when a part
// of it cannot be carried over.
QString templateFromNamingRule(const QList<MusicBeeNamingPart> &parts);

struct SettingsImportLine {
    QString what; // what it is in MusicBee, for the user to read
    QString key; // the linuxBee setting; empty when nothing is carried over
    QString value;
    QString note; // why not, or a remark

    bool imported() const { return !key.isEmpty(); }
    bool operator==(const SettingsImportLine &other) const = default;
};

// SI-PLAN: every hotkey and every naming rule linuxBee knows about, one line each.
QList<SettingsImportLine> planSettingsImport(const MusicBeeSettings &settings);

// SI-APPLY: writes the imported lines. Returns how many were written.
int applySettingsImport(const QList<SettingsImportLine> &plan, QSettings &target);

}
