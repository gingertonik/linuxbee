#pragma once
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

#include "playlists/playliststore.h"

class QPoint;

// Playlists made, renamed, moved and deleted from the navigator.
// Contract: docs/spec/playlist-editing.md (PE-x). It owns no rule: names
// and files are the playlist store's business (playlists.md).
namespace linuxbee::ui {

class ActionRegistry;
class LibraryShell;
class MainWindow;

class PlaylistEditing : public QObject {
    Q_OBJECT
public:
    // PE-DUPLICATES: the answers to "some are in the playlist already".
    enum class DuplicateChoice { AddAgain, Skip, Cancel };

    // What must be asked of the user. Tests replace all of them.
    struct Hooks {
        // A name, with a title for the question and the text to start from.
        // No value: the user gave up.
        std::function<std::optional<QString>(const QString &title, const QString &initial)> askName;
        // One of `folders`, or the empty text for "no folder". No value: gave up.
        std::function<std::optional<QString>(const QStringList &folders)> chooseFolder;
        std::function<bool(const QString &question)> confirm;
        // Shows the menu and returns the command chosen, or an empty text.
        std::function<QString(
            const QStringList &commands, const QStringList &labels, const QPoint &globalPos)>
            showMenu;
        // PE-ADD: one of the playlist `ids`, or the empty text for a new playlist. No value: gave
        // up.
        std::function<std::optional<QString>(const QStringList &ids)> choosePlaylist;
        // PE-DUPLICATES: `count` of the tracks being added are in the playlist `id` already.
        std::function<DuplicateChoice(const QString &id, int count)> askDuplicates;
        // PE-DUPLICATES (W-GP): whether that is asked at all. None: ticked, the default.
        std::function<bool()> warnOnDuplicates;
    };

    // Nothing handed in is owned; all of it must outlive this object.
    PlaylistEditing(MainWindow *window, ActionRegistry *actions, LibraryShell *shell,
        PlaylistStore *playlists, Hooks hooks = { }, QObject *parent = nullptr);
    ~PlaylistEditing() override;

    // PE-MENU: the commands offered on a navigator row, in menu order, and
    // their labels. Empty for a row that has no menu.
    QStringList commandsFor(const QString &rowId) const;
    static QString label(const QString &command);

    // PE-COMMANDS: carries one out. False when it was refused, failed, or
    // the user gave up; lastMessage() says why when there is a reason.
    bool run(const QString &command, const QString &rowId);

    bool addSelected(); // PE-ADD: the selected tracks, to a playlist that is asked for
    QString lastMessage() const; // PE-MESSAGE

private:
    struct Private;
    Private *d;
};

}
