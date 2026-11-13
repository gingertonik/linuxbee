#include "organise/organisedialog_p.h"

#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>

namespace linuxbee::ui {

QList<PlannedMove> OrganiseDialog::preview() const
{
    return d->plan;
}

// OD-TEMPLATE and OD-PREVIEW: ask the planner for the current options and show
// its answer. Planning reads tags and writes nothing, so this is safe on every
// change.
void OrganiseDialog::refreshPreview()
{
    d->previewTable->clear();
    d->plan.clear();

    const Template parsed = Template::parse(templateText());
    if (!parsed.isValid()) {
        d->problemLabel->setText(tr("Template: %1 (at character %2)")
                .arg(parsed.error().message)
                .arg(parsed.error().position + 1));
        d->summaryLabel->clear();
        d->runButton->setEnabled(false);
        return;
    }
    d->problemLabel->clear();

    d->plan = d->planner(d->files, options());

    int toMove = 0;
    int inPlace = 0;
    int cannot = 0;
    for (const PlannedMove &m : d->plan) {
        QString word;
        switch (m.status) {
        case PlannedMove::Status::Ready:
            word = m.replaces ? tr("Replaces a file") : m.renamed ? tr("Renamed") : tr("Ready");
            ++toMove;
            break;
        case PlannedMove::Status::AlreadyInPlace:
            word = tr("Already in place");
            ++inPlace;
            break;
        case PlannedMove::Status::MissingTags:
            word = tr("Missing: %1").arg(m.missingFields.join(QStringLiteral(", ")));
            ++cannot;
            break;
        case PlannedMove::Status::EmptyName:
            word = tr("No file name");
            ++cannot;
            break;
        case PlannedMove::Status::InvalidTemplate:
            word = tr("Invalid template");
            ++cannot;
            break;
        case PlannedMove::Status::NotFound:
            word = tr("File not found");
            ++cannot;
            break;
        case PlannedMove::Status::NotAudio:
            word = tr("Not a music file");
            ++cannot;
            break;
        }
        const bool hasTarget = m.status == PlannedMove::Status::Ready
            || m.status == PlannedMove::Status::AlreadyInPlace;
        new QTreeWidgetItem(
            d->previewTable, QStringList { m.source, hasTarget ? m.target : QString(), word });
    }

    if (d->copyRadio->isChecked())
        d->summaryLabel->setText(tr("%1 to copy, %2 already in place, %3 cannot be done")
                .arg(toMove)
                .arg(inPlace)
                .arg(cannot));
    else
        d->summaryLabel->setText(tr("%1 to move, %2 already in place, %3 cannot be done")
                .arg(toMove)
                .arg(inPlace)
                .arg(cannot));
    d->runButton->setEnabled(toMove > 0);
}

}
