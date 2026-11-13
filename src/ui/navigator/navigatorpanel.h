#pragma once
#include <QMap>
#include <QString>
#include <QStringList>
#include <QWidget>

// The left sidebar: the library's sections and the playlists, as a tree.
// Contract: docs/spec/navigator.md (V-x). It is told what exists and says
// what the user chose; it reads no library and no folder itself.
namespace linuxbee::ui {

class NavigatorPanel : public QWidget {
    Q_OBJECT
public:
    explicit NavigatorPanel(QWidget *parent = nullptr);
    ~NavigatorPanel() override;

    // V-LIBRARY: how many tracks each section holds. Sections not named count 0.
    void setLibraryCounts(const QMap<QString, int> &counts);
    // V-PLAYLISTS: playlist names as the playlist store lists them, folders
    // separated by '/', e.g. "Rock/Best of". Replaces the whole list.
    void setPlaylists(const QStringList &names);

    // V-NODES: "library:Music", "playlist:Rock/Best of", "playing"; empty when nothing is chosen.
    QString currentNode() const;
    bool selectNode(const QString &nodeId); // false: no such node (the choice stays)
    QStringList nodes() const; // every node that can be chosen, top to bottom

signals:
    void nodeSelected(const QString &nodeId); // the choice changed, by hand or by selectNode
    void nodeActivated(const QString &nodeId); // double-click or Return on a node

private:
    struct Private;
    Private *d;
};

}
