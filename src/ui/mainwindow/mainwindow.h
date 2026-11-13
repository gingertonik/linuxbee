#pragma once
#include <QMainWindow>
#include <QString>
#include <QStringList>

class QSettings;
class QSplitter;
class QStackedWidget;

// The main window: the menu bar, three panels side by side, the player bar
// and the status bar. Contract: docs/spec/main-window.md (W-x). It is a
// frame; later plans put the navigator, the track list, the playing tracks
// and the player controls into it.
namespace linuxbee::ui {

class ActionRegistry;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    enum class Panel { Navigator, Right, PlayerBar };

    // Neither pointer is owned; both must outlive the window.
    MainWindow(ActionRegistry *actions, QSettings *settings, QWidget *parent = nullptr);
    ~MainWindow() override;

    // W-PANELS: puts a widget into a panel, replacing what was there (the old
    // widget is deleted). The window owns the widget from then on.
    void setPanelWidget(Panel panel, QWidget *widget);
    QWidget *panelWidget(Panel panel) const;

    // W-PAGES: the middle of the window shows one page at a time.
    bool addPage(const QString &pageId, QWidget *page); // false: the id is taken or empty
    bool showPage(const QString &pageId);
    QString currentPage() const;
    QStringList pages() const; // in the order added

    // W-LAYOUT
    void saveLayout();
    void restoreLayout();

signals:
    void currentPageChanged(const QString &pageId);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    struct Private;
    Private *d;

    // W-COMMANDS / W-LAYOUT (mainwindow_commands.cpp).
    void implementWindowCommands();
    void setPanelVisibility(int panelIndex, bool shown);
    void applyPanelVisibility();
    void captureWidths();
};

}
