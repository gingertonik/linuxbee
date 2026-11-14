#pragma once
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

#include "library/library.h"

// Watches folders and has them scanned when files arrive in them: the
// "monitored folders" and the Inbox. Contract: docs/spec/folder-monitor.md
// (FM-x). It decides when; what a scan does is the library's rule.
namespace linuxbee {

// FM-DIRS: the roots that exist and every folder below them.
QStringList directoriesToWatch(const QStringList &roots, int limit, bool *limited = nullptr);

// FM-ROOTS: the roots that hold any of the changed folders, in the order of `roots`.
QStringList rootsFor(const QStringList &changedDirectories, const QStringList &roots);

class FolderMonitor : public QObject {
    Q_OBJECT
public:
    using Scanner = std::function<ScanReport(const QStringList &folders)>;

    explicit FolderMonitor(Scanner scanner, QObject *parent = nullptr);
    ~FolderMonitor() override;

    // FM-SETTLE: how long a folder must look the same before it is scanned.
    int settleMs() const; // 2000 unless set
    void setSettleMs(int ms); // below 0 counts as 0
    int directoryLimit() const; // 4096 unless set
    void setDirectoryLimit(int limit);

    QStringList folders() const;
    void setFolders(const QStringList &roots); // FM-WATCH
    QStringList watched() const; // the folders being watched now, sorted
    bool limited() const; // FM-DIRS: there were more folders than the limit
    bool isPending() const; // a change was seen and its scan has not happened yet

public slots:
    void checkNow(); // FM-CHECK: scan every root at once

signals:
    void scanned(const linuxbee::ScanReport &report, const QStringList &folders);

private:
    struct Private;
    Private *d;
};

// FM-LIBRARY: a scanner that scans into a library, new files going to `category`.
FolderMonitor::Scanner libraryScanner(
    Library *library, const QString &category, std::function<QDateTime()> now);

}
