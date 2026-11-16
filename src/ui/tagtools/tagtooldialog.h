#pragma once
#include <QDialog>
#include <QList>

#include <functional>

#include "tagtools/toolpanel.h"

class QSettings;

// The dialog every tagging tool uses: the tool's options on top, below them
// what would change, and Apply. Contract: docs/spec/tag-tools-dialog.md (TT-x).
namespace linuxbee::ui {

class TagToolDialog : public QDialog {
    Q_OBJECT
public:
    // How one file is saved. By default linuxbee::writeTags with default options.
    using Saver = TagEditorDialog::Saver;

    // The dialog owns the panel. The settings, when given, are not owned.
    TagToolDialog(ToolPanel *panel, const QList<EditTarget> &targets, QSettings *settings = nullptr,
        QWidget *parent = nullptr);
    ~TagToolDialog() override;

    void setSaver(Saver saver);
    // TT-WARN: how a yes-or-no question is asked. By default a dialog whose default is "no".
    using Confirm = std::function<bool(const QString &question)>;
    void setConfirm(Confirm confirm);
    ToolPanel *panel() const;

    // What Apply would write now: one entry per target, in order.
    QList<TagChanges> pendingChanges() const;

    bool apply(); // TT-APPLY; true when the dialog closed

signals:
    // The targets whose files were written (WriteResult::Status::Ok).
    void saved(const QList<qint64> &ids);

private:
    struct Private;
    Private *d;
};

}
