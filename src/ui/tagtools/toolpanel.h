#pragma once
#include <QList>
#include <QString>
#include <QWidget>

#include "tageditor/tageditordialog.h"
#include "tags/tagwriter.h"

class QSettings;

// What one tagging tool shows in the tagging-tool dialog: its options, and
// the changes those options would make. Contract: docs/spec/tag-tools-dialog.md
// (TT-PANEL). A panel decides nothing itself: it hands its options to the
// functions of tagtools/tagtools.h.
namespace linuxbee::ui {

class ToolPanel : public QWidget {
    Q_OBJECT
public:
    using QWidget::QWidget;
    virtual QString title() const = 0; // the dialog's window title
    virtual QString problem() const = 0; // empty: the options can be used
    // One entry per target, in order; all empty while there is a problem.
    virtual QList<TagChanges> changes(const QList<EditTarget> &targets) const = 0;
    virtual void load(QSettings &settings) = 0; // TT-REMEMBER
    virtual void save(QSettings &settings) const = 0;

signals:
    void optionsChanged(); // whenever an option changed, by the user or by load()
};

}
