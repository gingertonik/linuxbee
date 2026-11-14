#pragma once
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

class QPoint;

namespace linuxbee {
class AutoPlaylistStore;
class Library;
}

// Auto-playlists as nodes of the navigator: listed, shown when chosen,
// deleted from the row's menu. Contract: docs/spec/auto-playlist-nodes.md
// (AN-NODES, AN-SHOW, AN-MENU). What an auto-playlist holds is decided by
// evaluateAutoPlaylist (auto-playlists.md), not here.
namespace linuxbee::ui {

class LibraryShell;
class MainWindow;

class AutoPlaylistNodes : public QObject {
    Q_OBJECT
public:
    struct Hooks {
        std::function<QDateTime()> now; // default: the clock
        std::function<quint32()> seed; // asked once; default: a random number
        std::function<bool(const QString &question)> confirm;
        std::function<QString(
            const QStringList &commands, const QStringList &labels, const QPoint &globalPos)>
            showMenu;
    };

    // Nothing handed in is owned; all of it must outlive this object.
    AutoPlaylistNodes(MainWindow *window, LibraryShell *shell, Library *library,
        AutoPlaylistStore *store, Hooks hooks = { }, QObject *parent = nullptr);
    ~AutoPlaylistNodes() override;

    // AN-MENU
    QStringList commandsFor(const QString &rowId) const;
    static QString label(const QString &command);
    bool run(const QString &command, const QString &rowId);

    QString lastMessage() const;

private:
    struct Private;
    Private *d;
};

}
