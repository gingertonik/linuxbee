#include "navigator/navigatorpanel.h"

// Stub, so that the tree links before plan 11d is built. Plan 11d moves
// these two functions into navigatorpanel.cpp, where the tree lives, and
// deletes this file (`git rm`).
namespace linuxbee::ui {

QString NavigatorPanel::rowAt(const QPoint &viewportPos) const
{
    Q_UNUSED(viewportPos);
    return { };
}

QStringList NavigatorPanel::rows() const
{
    return { };
}

}
