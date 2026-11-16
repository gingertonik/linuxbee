#pragma once

#include <QImage>
#include <QObject>
#include <QString>

#include <functional>

// Covers for the window, small and ready to draw: found and scaled on a
// thread of its own, kept for the next time. Contract: docs/spec/covers.md
// (CC-x). Which picture stands for a track is the core's rule (CV-FIND).
namespace linuxbee::ui {

struct CoverCacheHooks {
    // CC-THREAD: called on the loading thread before a file is looked at. For tests.
    std::function<void(const QString &musicFile)> loading;
};

class CoverCache : public QObject {
    Q_OBJECT
public:
    explicit CoverCache(int edge = 64, CoverCacheHooks hooks = { }, QObject *parent = nullptr);
    ~CoverCache() override; // CC-THREAD: waits for the load under way; nothing is emitted after

    int edge() const; // the pictures fit a square of this many pixels
    // CC-ASK: the cover of this music file as kept; a null image when it has
    // none or is not kept yet. Not kept (or kept under another stamp): a load
    // is asked for. Never reads a file itself.
    QImage cover(const QString &musicFile, qint64 stamp = 0);
    bool has(const QString &musicFile, qint64 stamp = 0) const; // kept, with or without a picture

    int limit() const; // CC-KEEP: 512 unless set
    void setLimit(int entries); // below 1 counts as 1
    int size() const; // entries kept
    int waitingLimit() const; // CC-ASK: 64 unless set
    void setWaitingLimit(int requests); // below 1 counts as 1
    int waiting() const; // loads asked for and not begun
    int loads() const; // CC-ASK: loads begun since the cache was made

signals:
    void loaded(const QString &musicFile); // kept now, with or without a picture

private:
    struct Private;
    Private *d;
};

}
