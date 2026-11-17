// LOCKED INTERFACE. Written by the reviewer, not the builder.
// PS-COUNT (docs/spec/playback-shell.md): plays and skips on their way into the library. One the
// library refused is kept and offered again, so that a library held for a moment by another program
// costs no play count, and the window never waits long for one.
#pragma once

#include <QDateTime>
#include <QObject>

#include <memory>

namespace linuxbee {
class Library;
}

namespace linuxbee::ui {

class PendingPlays : public QObject {
    Q_OBJECT

public:
    static constexpr int kMost = 1000; // how many are kept; then the oldest goes

    // The library is not owned and must outlive this.
    explicit PendingPlays(linuxbee::Library *library, QObject *parent = nullptr);
    ~PendingPlays() override;

    // A play or a skip that has happened. What is kept is offered first (as retry does); if any of
    // it is still kept, this one joins it untried, so that one call asks the library to wait at
    // most once.
    void play(qint64 trackId, const QDateTime &when);
    void skip(qint64 trackId, const QDateTime &when);

    // Offers what is kept again, oldest first, and stops at the first the library refuses.
    void retry();

    int kept() const;

signals:
    void keptChanged(int kept); // after a call that left another number kept
    void oldestDropped(); // the first time one is let go because kMost are kept; once in this
                          // object's life

private:
    struct Private;
    std::unique_ptr<Private> d;
};

}
