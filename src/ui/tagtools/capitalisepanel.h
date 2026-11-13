#pragma once
#include "tagtools/tagtools.h"
#include "tagtools/toolpanel.h"

// Capitalise. Contract: docs/spec/tag-tools-dialog.md (TT-CASE).
namespace linuxbee::ui {

class CapitalisePanel : public ToolPanel {
    Q_OBJECT
public:
    explicit CapitalisePanel(QWidget *parent = nullptr);
    ~CapitalisePanel() override;

    QString title() const override;
    QString problem() const override;
    QList<TagChanges> changes(const QList<EditTarget> &targets) const override;
    void load(QSettings &settings) override;
    void save(QSettings &settings) const override;

    CaseOptions options() const; // what the controls say now

private:
    struct Private;
    Private *d;
};

}
