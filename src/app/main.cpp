#include <QApplication>
#include <QMainWindow>
#include <QTextStream>

#include "app/appinfo.h"

// Until the main window plan lands, this shows an empty window. It exists so
// that the Widgets half builds, links and starts in every gate run.
int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--version") == 0) {
            QTextStream(stdout) << linuxbee::ui::versionLine() << '\n';
            return 0;
        }
    }
    QApplication application(argc, argv);
    linuxbee::ui::applyApplicationInfo(application);
    QMainWindow window;
    window.setObjectName(QStringLiteral("mainWindow"));
    window.setWindowTitle(linuxbee::ui::applicationDisplayName());
    window.resize(1100, 700);
    window.show();
    return application.exec();
}
