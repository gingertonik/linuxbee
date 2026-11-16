#pragma once
#include "tagtools/tagtools.h"
#include "tagtools/toolpanel.h"

// Infer Tags From Filename. Contract: docs/spec/tag-tools-dialog.md (TT-INFER).
namespace linuxbee::ui {

class InferPanel : public ToolPanel {
    Q_OBJECT
public:
    explicit InferPanel(QWidget *parent = nullptr);
    ~InferPanel() override;

    QString title() const override;
    QString problem() const override;
    QList<TagChanges> changes(const QList<EditTarget> &targets) const override;
    void load(QSettings &settings) override;
    void save(QSettings &settings) const override;

    QString pattern() const; // what the controls say now
    bool blankFieldsOnly() const;

private:
    struct Private;
    Private *d;
};

}
