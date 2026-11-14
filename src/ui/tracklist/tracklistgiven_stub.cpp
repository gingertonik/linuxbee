#include "tracklist/tracklistmodel.h"

// STUB (review-30-order). The builder moves these two into the model's own
// files with real bodies and removes this file.
namespace linuxbee::ui {

bool TrackListModel::givenOrder() const
{
    return false;
}

void TrackListModel::setGivenOrder(bool)
{
}

}
