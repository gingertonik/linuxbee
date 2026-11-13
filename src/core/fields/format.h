#pragma once
#include <QString>

namespace linuxbee::fieldformat {

// F-TIME: rounds to the nearest second (halves up; a negative length counts
// as 0). Prints m:ss under an hour and h:mm:ss from an hour up.
QString duration(qint64 milliseconds);

}
