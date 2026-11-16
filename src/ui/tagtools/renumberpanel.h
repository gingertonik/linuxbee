#pragma once
#include "tagtools/tagtools.h"
#include "tagtools/toolpanel.h"

// Renumber Tracks. Contract: docs/spec/tag-tools-dialog.md (TT-RENUMBER).
namespace linuxbee::ui {

class RenumberPanel : public ToolPanel {
    Q_OBJECT
public:
    explicit RenumberPanel(QWidget *parent = nullptr);
    ~RenumberPanel() override;

    QString title() const override;
    QString problem() const override;
    QList<TagChanges> changes(const QList<EditTarget> &targets) const override;
    void load(QSettings &settings) override;
    void save(QSettings &settings) const override;

    RenumberOptions options() const; // what the controls say now

private:
    struct Private;
    Private *d;
};

}
