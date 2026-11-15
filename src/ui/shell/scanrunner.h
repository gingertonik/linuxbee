#pragma once
#include <QObject>
#include <QStringList>

#include <functional>

#include "library/library.h"

// A scan that leaves the window free: looking at files on another thread,
// storing on the library's. Contract: docs/spec/scan-runner.md (SR-x).
namespace linuxbee::ui {

struct ScanRunnerHooks {
    // SR-THREADS: called on the looking thread before each file. For tests.
    std::function<void(int done, int total)> looking;
};

class ScanRunner : public QObject {
    Q_OBJECT
public:
    // The library is not owned and must outlive the runner; the runner
    // must be made on the library's thread.
    explicit ScanRunner(Library *library, ScanRunnerHooks hooks = { }, QObject *parent = nullptr);
    ~ScanRunner() override; // SR-STOP

    bool isRunning() const; // SR-SCAN: a scan is under way or waiting
    int waiting() const;
    int partSize() const;
    void setPartSize(int files); // for the scans asked for after this

    void scan(const QStringList &folders, const ScanOptions &options); // SR-SCAN
    void cancel(); // SR-STOP

signals:
    void started(const QStringList &folders);
    void progress(int done, int total);
    void finished(const linuxbee::ScanReport &report, const QStringList &folders);
    void idle();

private:
    struct Private;
    Private *d;
};

}

Q_DECLARE_METATYPE(linuxbee::ScanReport)
