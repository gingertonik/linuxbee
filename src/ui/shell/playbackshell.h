#pragma once
#include <QDateTime>
#include <QObject>
#include <QString>

#include <functional>

#include "library/library.h"
#include "playback/player.h"
#include "playback/playqueue.h"

class QSettings;

// Puts playback into the window: the play queue, the player and the player
// bar, connected to the library shell. Contract:
// docs/spec/playback-shell.md (PS-x). It owns no rule of its own.
namespace linuxbee::ui {

class ActionRegistry;
class LibraryShell;
class MainWindow;
class PlayerBar;

class PlaybackShell : public QObject {
    Q_OBJECT
public:
    // Nothing handed in is owned; all of it must outlive the shell. The
    // backend is what makes the sound (tests hand in one that only records).
    PlaybackShell(MainWindow *window, ActionRegistry *actions, Library *library,
        LibraryShell *libraryShell, AudioBackend *backend, QSettings *settings,
        std::function<QDateTime()> now, QObject *parent = nullptr);
    ~PlaybackShell() override;

    PlayQueue *queue() const;
    Player *player() const;
    PlayerBar *bar() const;

    // PS-TITLE: what the player bar is told for a path.
    QString titleFor(const QString &path) const;
    QString lastMessage() const; // PS-ERROR: what the status bar was last told

    void saveState(); // PS-STORE

private:
    struct Private;
    Private *d;
};

}
