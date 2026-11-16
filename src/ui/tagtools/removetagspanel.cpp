#include "tagtools/removetagspanel.h"

// Stub from the seed: the builder replaces this file. Contract: docs/spec/tag-tools-dialog.md.
namespace linuxbee::ui {

struct RemoveTagsPanel::Private { };

RemoveTagsPanel::RemoveTagsPanel(QWidget *parent)
    : ToolPanel(parent)
    , d(new Private)
{
}

RemoveTagsPanel::~RemoveTagsPanel()
{
    delete d;
}

QString RemoveTagsPanel::title() const
{
    return QString();
}

QString RemoveTagsPanel::problem() const
{
    return QString();
}

QList<TagChanges> RemoveTagsPanel::changes(const QList<EditTarget> &targets) const
{
    return QList<TagChanges>(targets.size());
}

void RemoveTagsPanel::load(QSettings &)
{
}

void RemoveTagsPanel::save(QSettings &) const
{
}

RemoveOptions RemoveTagsPanel::options() const
{
    return RemoveOptions();
}

}
