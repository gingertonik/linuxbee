#pragma once
#include <QString>

class QCoreApplication;

// What the application calls itself. One place, so the window title, the
// settings location and the desktop entry agree.
namespace linuxbee::ui {

QString applicationDisplayName(); // "linuxBee"
QString applicationSettingsName(); // "linuxbee": folder names, the desktop file
QString versionLine(); // "linuxBee 0.6.0"

// Sets the application name, display name, version and desktop file name.
void applyApplicationInfo(QCoreApplication &application);

}
