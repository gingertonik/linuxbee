#pragma once

#include <QObject>
#include <QString>

#include <functional>

// A row in the status bar while a scan runs: how far it is, and a way to
// stop it. Contract: docs/spec/scan-runner.md (SP-x). It owns no rule of a
// scan: it listens to scan runners and asks them to stop.
namespace linuxbee::ui {

class ActionRegistry;
class MainWindow;
class ScanRunner;

class ScanProgress : public QObject {
    Q_OBJECT
public:
    struct Hooks {
        // A yes-or-no question before a scan is stopped. Tests replace it.
        std::function<bool(const QString &question)> confirm;
    };

    // Nothing handed in is owned; the window and the registry must outlive this object.
    ScanProgress(
        MainWindow *window, ActionRegistry *actions, Hooks hooks = { }, QObject *parent = nullptr);
    ~ScanProgress() override;

    void watch(ScanRunner *runner); // SP-WATCH; a runner that is destroyed is forgotten
    bool isShown() const; // SP-ROW: a watched runner is scanning
    bool stop(); // SP-STOP; true when the scans were told to stop
    int updates() const; // SP-ONCE: how often the row's widgets were written

private:
    struct Private;
    Private *d;
};

}
