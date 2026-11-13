#include "navigator/navigatorpanel.h"

// Stub. Plan 11b replaces every body in this file.
namespace linuxbee::ui {

struct NavigatorPanel::Private { };

NavigatorPanel::NavigatorPanel(QWidget *parent)
    : QWidget(parent)
    , d(nullptr)
{
}

NavigatorPanel::~NavigatorPanel()
{
    Q_UNUSED(d);
}

void NavigatorPanel::setLibraryCounts(const QMap<QString, int> &counts)
{
    Q_UNUSED(counts);
}

void NavigatorPanel::setPlaylists(const QStringList &names)
{
    Q_UNUSED(names);
}

QString NavigatorPanel::currentNode() const
{
    return { };
}

bool NavigatorPanel::selectNode(const QString &nodeId)
{
    Q_UNUSED(nodeId);
    return false;
}

QStringList NavigatorPanel::nodes() const
{
    return { };
}

}
