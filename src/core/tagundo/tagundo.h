#pragma once
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

#include "tags/tagwriter.h"

// One step of undo for tag changes: what the changed entries of each file
// held before the last change, and putting that back.
// Contract: docs/spec/tag-undo.md (UN-x). It shows nothing and asks nothing.
namespace linuxbee {

class TagUndo : public QObject {
    Q_OBJECT
public:
    // How one file is saved. An empty one is linuxbee::writeTags with default options.
    using Writer = std::function<WriteResult(const QString &path, const TagChanges &changes)>;

    // Removed pictures are kept as files in `scratchFolder` (made when first
    // needed) until the step is forgotten. Nothing else is ever put there,
    // and only files this object made are removed from it.
    explicit TagUndo(const QString &scratchFolder, QObject *parent = nullptr);
    ~TagUndo() override; // forgets the step, and its files

    // UN-RECORD. One change of many files is: begin, a write for each file, end.
    void begin(const QString &name);
    WriteResult write(const QString &path, const TagChanges &changes, const Writer &writer = { });
    void end();

    // UN-STEP
    bool canUndo() const;
    QString name() const; // of the step that undo() would take back; empty when there is none
    QStringList paths() const; // its files, in the order first written

    struct Outcome {
        QStringList restored; // the files that hold their earlier values again
        QStringList failed; // the files that could not be written
        // UN-SAME: the files that are no longer the file that was edited, and were left alone
        QStringList changed;
    };
    Outcome undo(const Writer &writer = { });

    // UN-MOVED: files have other paths now (old path to new path).
    void moved(const QHash<QString, QString> &oldToNew);

signals:
    void changed(); // canUndo(), name() or paths() may answer differently now

private:
    struct Private;
    Private *d;
};

}
