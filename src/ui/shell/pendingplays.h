// LOCKED INTERFACE. Written by the reviewer, not the builder.
// PS-COUNT (docs/spec/playback-shell.md): plays and skips on their way into the library. One the
// library refused is kept and offered again, so that a library held for a moment by another program
// costs no play count, and the window never waits long for one.
#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>

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
    // PS-COUNT: what is still kept is lost here, and that is said, once, as a warning.
    ~PendingPlays() override;

    // A play or a skip that has happened, of the file at `path`. It joins what is kept, and all of
    // it is offered to the library in one call (Library::recordPlays), which stores all of it or
    // gives back what it refused; that is what is kept. This object reads nothing from the library
    // itself, and a file the library does not have is never kept.
    void play(const QString &path, const QDateTime &when);
    void skip(const QString &path, const QDateTime &when);

    // Offers what is kept again, in one change. Nothing kept: the library is not asked.
    void retry();

    int kept() const;

signals:
    void keptChanged(int kept); // after a call that left another number kept
    // One was let go because kMost are kept: the first time since nothing was kept.
    void oldestDropped();

private:
    struct Private;
    std::unique_ptr<Private> d;
};

}
