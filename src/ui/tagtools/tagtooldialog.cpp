#include "tagtools/tagtooldialog.h"

// Stub. Plan 12b replaces every body in this file.
namespace linuxbee::ui {

struct TagToolDialog::Private {
    ToolPanel *panel = nullptr;
};

TagToolDialog::TagToolDialog(
    ToolPanel *panel, const QList<EditTarget> &targets, QSettings *settings, QWidget *parent)
    : QDialog(parent)
    , d(new Private)
{
    d->panel = panel;
    if (panel)
        panel->setParent(this);
    Q_UNUSED(targets);
    Q_UNUSED(settings);
}

TagToolDialog::~TagToolDialog()
{
    delete d;
}

void TagToolDialog::setSaver(Saver saver)
{
    Q_UNUSED(saver);
}

ToolPanel *TagToolDialog::panel() const
{
    return d->panel;
}

QList<TagChanges> TagToolDialog::pendingChanges() const
{
    return { };
}

bool TagToolDialog::apply()
{
    return false;
}

}
