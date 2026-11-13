#include "tagtools/capitalisepanel.h"

// Stub. Plan 12b replaces every body in this file.
namespace linuxbee::ui {

struct CapitalisePanel::Private { };

CapitalisePanel::CapitalisePanel(QWidget *parent)
    : ToolPanel(parent)
    , d(nullptr)
{
}

CapitalisePanel::~CapitalisePanel()
{
    Q_UNUSED(d);
}

QString CapitalisePanel::title() const
{
    return { };
}

QString CapitalisePanel::problem() const
{
    return { };
}

QList<TagChanges> CapitalisePanel::changes(const QList<EditTarget> &targets) const
{
    Q_UNUSED(targets);
    return { };
}

void CapitalisePanel::load(QSettings &settings)
{
    Q_UNUSED(settings);
}

void CapitalisePanel::save(QSettings &settings) const
{
    Q_UNUSED(settings);
}

CaseOptions CapitalisePanel::options() const
{
    return { };
}

}
