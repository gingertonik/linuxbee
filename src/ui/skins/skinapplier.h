#pragma once
#include <QObject>
#include <QString>

class QApplication;
class QSettings;

// Gives the application the colours of a skin file. Contract:
// docs/spec/skins.md (SK-APPLY). The rules are in the core (skin/skin.h);
// this reads the files and hands the result to Qt.
namespace linuxbee::ui {

class SkinApplier : public QObject {
    Q_OBJECT
public:
    // Neither is owned; both must outlive this object. The stored skin, if
    // any, is applied at once.
    SkinApplier(QApplication *application, QSettings *settings, QObject *parent = nullptr);
    ~SkinApplier() override;

    QString currentFile() const; // empty: no skin, linuxBee's own look
    QString basis() const; // SK-APPLY: "", "none", "read" or "unreadable"
    QString lastError() const; // why the last apply() was refused

    bool apply(const QString &file); // false: refused, nothing changed
    void clear(); // back to linuxBee's own look

signals:
    void changed();

private:
    struct Private;
    Private *d;
};

}
