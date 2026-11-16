#pragma once
#include "tagtools/tagtools.h"
#include "tagtools/toolpanel.h"

// Remove Tags. Contract: docs/spec/tag-tools-dialog.md (TT-REMOVE).
namespace linuxbee::ui {

class RemoveTagsPanel : public ToolPanel {
    Q_OBJECT
public:
    explicit RemoveTagsPanel(QWidget *parent = nullptr);
    ~RemoveTagsPanel() override;

    QString title() const override;
    QString problem() const override;
    QList<TagChanges> changes(const QList<EditTarget> &targets) const override;
    void load(QSettings &settings) override;
    void save(QSettings &settings) const override;
    QString warning(int tracks) const override; // TT-REMOVE

    RemoveOptions options() const; // what the controls say now

private:
    struct Private;
    Private *d;
};

}
