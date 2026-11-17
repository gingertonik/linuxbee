#include "tageditor/tageditordialog.h"

#include "tageditor/tageditordialog_p.h"
#include "tagedit/tagedit.h"

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace linuxbee::ui {

namespace {

QList<TrackTags> tagsOf(const QList<EditTarget> &targets)
{
    QList<TrackTags> tags;
    for (const EditTarget &target : targets)
        tags.append(target.tags);
    return tags;
}

}

TagEditorDialog::TagEditorDialog(const QList<EditTarget> &targets, QWidget *parent)
    : QDialog(parent)
    , d(new Private)
{
    setObjectName(QStringLiteral("tagEditorDialog"));
    setWindowTitle(tr("Edit Tags"));
    d->targets = targets;
    d->saver = [](const QString &path, const TagChanges &changes) {
        return linuxbee::writeTags(path, changes);
    };

    auto *layout = new QVBoxLayout(this);

    d->summary = new QLabel(this);
    d->summary->setObjectName(QStringLiteral("summaryLabel"));
    if (targets.size() == 1)
        d->summary->setText(QFileInfo(targets.first().path).fileName());
    else
        d->summary->setText(tr("%1 files").arg(targets.size()));
    layout->addWidget(d->summary);

    const QList<TrackTags> tracks = tagsOf(targets);
    auto *form = new QFormLayout;
    for (const QString &field : tagEditorFields()) {
        const FieldState state = fieldState(tracks, field);
        d->states.insert(field, state);

        auto *edit = new QLineEdit(this);
        edit->setObjectName(QStringLiteral("field.") + field);
        if (state.kind == FieldState::Kind::Mixed)
            edit->setPlaceholderText(tr("<multiple values>"));
        else if (field == QLatin1String("Love"))
            edit->setPlaceholderText(tr("L (loved) or B (banned)"));
        edit->setText(state.text);
        edit->setEnabled(!targets.isEmpty());

        auto *label = new QLabel(field, this);
        label->setBuddy(edit);
        form->addRow(label, edit);
        d->editors.insert(field, edit);
    }
    layout->addLayout(form);

    d->problem = new QLabel(this);
    d->problem->setObjectName(QStringLiteral("problemLabel"));
    layout->addWidget(d->problem);

    auto *buttons = new QDialogButtonBox(this);
    buttons->setObjectName(QStringLiteral("buttons"));
    d->saveButton = buttons->addButton(tr("Save"), QDialogButtonBox::AcceptRole);
    d->saveButton->setObjectName(QStringLiteral("saveButton"));
    d->saveButton->setDefault(true);
    d->saveButton->setEnabled(!targets.isEmpty());
    d->cancelButton = buttons->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);
    d->cancelButton->setObjectName(QStringLiteral("cancelButton"));
    layout->addWidget(buttons);

    connect(d->saveButton, &QPushButton::clicked, this, [this] { save(); });
    connect(d->cancelButton, &QPushButton::clicked, this, &QDialog::reject);
}

TagEditorDialog::~TagEditorDialog()
{
    delete d;
}

void TagEditorDialog::setSaver(Saver saver)
{
    d->saver = std::move(saver);
}

QMap<QString, QString> TagEditorDialog::edits() const
{
    QMap<QString, QString> typed;
    for (const QString &field : tagEditorFields()) {
        const QLineEdit *edit = d->editors.value(field);
        if (!edit)
            continue;
        // E-DIALOG: a field that showed several values counts as edited once typed in;
        // any other field counts as edited when its text differs from what it showed.
        const FieldState state = d->states.value(field);
        const bool edited = state.kind == FieldState::Kind::Mixed ? edit->isModified()
                                                                  : edit->text() != state.text;
        if (edited)
            typed.insert(field, edit->text());
    }
    return typed;
}

QList<TagChanges> TagEditorDialog::pendingChanges() const
{
    return buildEdits(tagsOf(d->targets), edits());
}

}
