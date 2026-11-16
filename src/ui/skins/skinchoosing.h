#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

// Choosing a skin in the window, and adding a skin file to the skins
// folder. Contract: docs/spec/skins.md (SC-x). It owns no rule of what a
// skin is: reading is the core's (skin/skin.h), applying is SkinApplier's.
namespace linuxbee::ui {

class ActionRegistry;
class MainWindow;
class SkinApplier;

class SkinChoosing : public QObject {
    Q_OBJECT
public:
    // What must be asked of the user. Tests replace both.
    struct Hooks {
        // One of `files` (as skinFiles() gives them); `current` is the skin in
        // use, or empty. No value: the user gave up.
        std::function<std::optional<QString>(const QStringList &files, const QString &current)>
            chooseSkin;
        // A skin file from anywhere, to add. No value: the user gave up.
        std::function<std::optional<QString>()> chooseFile;
    };

    // Nothing handed in is owned; all of it must outlive this object.
    // `folder` is the skins folder; it need not exist.
    SkinChoosing(MainWindow *window, ActionRegistry *actions, SkinApplier *applier,
        const QString &folder, Hooks hooks = { }, QObject *parent = nullptr);
    ~SkinChoosing() override;

    QString folder() const;
    QStringList skinFiles() const; // SC-LIST: absolute paths, in the order to offer them
    bool choose(); // SC-CHOOSE
    bool add(); // SC-ADD
    bool none(); // SC-NONE
    QString lastMessage() const; // SC-MESSAGE

private:
    struct Private;
    Private *d;
};

}
