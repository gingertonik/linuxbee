#include "tageditor/tageditordialog.h"

// Stub. Plan 12 replaces every body in this file.
namespace linuxbee::ui {

struct TagEditorDialog::Private { };

TagEditorDialog::TagEditorDialog(const QList<EditTarget> &targets, QWidget *parent)
    : QDialog(parent)
    , d(nullptr)
{
    Q_UNUSED(targets);
}

TagEditorDialog::~TagEditorDialog()
{
    Q_UNUSED(d);
}

void TagEditorDialog::setSaver(Saver saver)
{
    Q_UNUSED(saver);
}

QMap<QString, QString> TagEditorDialog::edits() const
{
    return { };
}

QList<TagChanges> TagEditorDialog::pendingChanges() const
{
    return { };
}

bool TagEditorDialog::save()
{
    return false;
}

}
