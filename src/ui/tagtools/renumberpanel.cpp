#include "tagtools/renumberpanel.h"

// Stub from the seed: the builder replaces this file. Contract: docs/spec/tag-tools-dialog.md.
namespace linuxbee::ui {

struct RenumberPanel::Private { };

RenumberPanel::RenumberPanel(QWidget *parent)
    : ToolPanel(parent)
    , d(new Private)
{
}

RenumberPanel::~RenumberPanel()
{
    delete d;
}

QString RenumberPanel::title() const
{
    return QString();
}

QString RenumberPanel::problem() const
{
    return QString();
}

QList<TagChanges> RenumberPanel::changes(const QList<EditTarget> &targets) const
{
    return QList<TagChanges>(targets.size());
}

void RenumberPanel::load(QSettings &)
{
}

void RenumberPanel::save(QSettings &) const
{
}

RenumberOptions RenumberPanel::options() const
{
    return RenumberOptions();
}

}
