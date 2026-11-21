// fix-sr4a stub: the builder removes this file and makes these real.
#include "tracklist/tracklistmodel.h"

namespace linuxbee::ui {

bool TrackListModel::searchIgnoresAccents() const
{
    return true;
}

void TrackListModel::setSearchIgnoresAccents(bool)
{
}

}
