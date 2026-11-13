#pragma once
#include "tagtools/tagtools.h"
#include "tagtools/toolpanel.h"

// Search and Replace. Contract: docs/spec/tag-tools-dialog.md (TT-REPLACE).
namespace linuxbee::ui {

class SearchReplacePanel : public ToolPanel {
    Q_OBJECT
public:
    explicit SearchReplacePanel(QWidget *parent = nullptr);
    ~SearchReplacePanel() override;

    QString title() const override;
    QString problem() const override;
    QList<TagChanges> changes(const QList<EditTarget> &targets) const override;
    void load(QSettings &settings) override;
    void save(QSettings &settings) const override;

    SearchReplaceOptions options() const; // what the controls say now

private:
    struct Private;
    Private *d;
};

}
