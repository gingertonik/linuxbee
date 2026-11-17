#pragma once

class QCoreApplication;

namespace linuxbee::ui {

// W-SIGNAL (docs/spec/main-window.md): SIGTERM, SIGINT and SIGHUP end the
// application as File > Exit does: the event loop is left through quit(), so
// aboutToQuit saves what an ordinary exit saves. Call once, before exec().
void quitOnSignals(QCoreApplication *application);

}
