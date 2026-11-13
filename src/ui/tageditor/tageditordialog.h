#pragma once
#include <QDialog>
#include <QList>
#include <QMap>
#include <QString>

#include <functional>

#include "tags/tagreader.h"
#include "tags/tagwriter.h"

// The tag editor: one dialog for one track or many.
// Contract: docs/spec/tag-editor.md (E-DIALOG).
namespace linuxbee::ui {

struct EditTarget {
    qint64 id = 0; // the caller's id for the track; handed back by saved()
    QString path;
    TrackTags tags;
};

class TagEditorDialog : public QDialog {
    Q_OBJECT
public:
    // How one file is saved. By default linuxbee::writeTags with default options.
    using Saver = std::function<WriteResult(const QString &path, const TagChanges &changes)>;

    explicit TagEditorDialog(const QList<EditTarget> &targets, QWidget *parent = nullptr);
    ~TagEditorDialog() override;

    void setSaver(Saver saver);

    // What the user has changed so far: field name -> typed text.
    QMap<QString, QString> edits() const;
    // What Save would write: one entry per target, in order.
    QList<TagChanges> pendingChanges() const;

    // E-DIALOG, Save. True when the dialog closed.
    bool save();

signals:
    // The targets whose files were written (WriteResult::Status::Ok).
    void saved(const QList<qint64> &ids);

private:
    struct Private;
    Private *d;
};

}
