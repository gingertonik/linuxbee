#include "tagtools/searchreplacepanel.h"

// Stub. Plan 12b replaces every body in this file.
namespace linuxbee::ui {

struct SearchReplacePanel::Private { };

SearchReplacePanel::SearchReplacePanel(QWidget *parent)
    : ToolPanel(parent)
    , d(nullptr)
{
}

SearchReplacePanel::~SearchReplacePanel()
{
    Q_UNUSED(d);
}

QString SearchReplacePanel::title() const
{
    return { };
}

QString SearchReplacePanel::problem() const
{
    return { };
}

QList<TagChanges> SearchReplacePanel::changes(const QList<EditTarget> &targets) const
{
    Q_UNUSED(targets);
    return { };
}

void SearchReplacePanel::load(QSettings &settings)
{
    Q_UNUSED(settings);
}

void SearchReplacePanel::save(QSettings &settings) const
{
    Q_UNUSED(settings);
}

SearchReplaceOptions SearchReplacePanel::options() const
{
    return { };
}

}
