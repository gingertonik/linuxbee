#pragma once
#include <QSet>
#include <QString>
#include <QStringList>

// L-UNPLUGGED: the tables of mounts, as the scan reads them. Private to the library and its tests.
namespace linuxbee::mounts {

// The mount point in field `field` (counted from 0) of every line of a table: field 1 of fstab,
// field 4 of mountinfo. A file that cannot be read gives nothing.
QStringList mountFieldOf(const QString &table, int field);

// The folders the table (LINUXBEE_FSTAB or /etc/fstab) names as mount points, other than `/`.
QStringList tableMountPoints();

// What is mounted now: /proc/self/mountinfo, or the table LINUXBEE_MOUNTINFO names.
QSet<QString> mountedNow();

}
