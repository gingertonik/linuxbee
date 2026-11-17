// LOCKED INTERFACE. Written by the reviewer, not the builder.
// SH-REFUSED (docs/spec/library-shell.md): the one standing listener to the library's `failed`.
// What the library refused is said in the status bar, once for each return to the event loop, and
// never in a dialog.
#pragma once

#include <QObject>
#include <QString>

#include <memory>

class QStatusBar;

namespace linuxbee {
class Library;
}

namespace linuxbee::ui {

class RefusalNotice : public QObject {
    Q_OBJECT

public:
    // Neither is owned. Either may be destroyed first; the notice then does nothing.
    RefusalNotice(linuxbee::Library *library, QStatusBar *bar, QObject *parent = nullptr);
    ~RefusalNotice() override;

    QString lastLine() const; // the last line it put in the status bar; empty when it has put none
    int count() const; // how many lines it has put there

signals:
    void said(const QString &line); // after the line is in the status bar

private:
    struct Private;
    std::unique_ptr<Private> d;
};

}
