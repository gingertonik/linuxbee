#include "library/mounts.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QList>

// L-UNPLUGGED: the tables of mounts, moved out of scan.cpp (fix-m1b) so that a test can read the
// real one.
namespace linuxbee::mounts {
namespace {

// A mount point as the tables write it: a space, tab, newline or backslash is a backslash and
// three octal digits (040, 011, 012, 134).
QString unescapedMountPoint(const QByteArray &field)
{
    QByteArray out;
    for (qsizetype i = 0; i < field.size(); ++i) {
        const auto digit = [&field](qsizetype at) {
            return at < field.size() && field.at(at) >= '0' && field.at(at) <= '7';
        };
        if (field.at(i) == '\\' && digit(i + 1) && digit(i + 2) && digit(i + 3)) {
            out.append(char(((field.at(i + 1) - '0') << 6) | ((field.at(i + 2) - '0') << 3)
                | (field.at(i + 3) - '0')));
            i += 3;
        } else {
            out.append(field.at(i));
        }
    }
    return QDir::cleanPath(QString::fromUtf8(out));
}

}

QStringList mountFieldOf(const QString &table, int field)
{
    QStringList points;
    QFile file(table);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return points;
    // A file under /proc has size 0 and atEnd() is true at once there: read lines until one comes
    // back empty (a blank line is "\n", not empty).
    for (QByteArray raw = file.readLine(); !raw.isEmpty(); raw = file.readLine()) {
        const QByteArray line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        const QList<QByteArray> words = line.simplified().split(' ');
        if (field < words.size())
            points.append(unescapedMountPoint(words.at(field)));
    }
    return points;
}

QStringList tableMountPoints()
{
    const QByteArray other = qgetenv("LINUXBEE_FSTAB");
    const QString table = other.isEmpty() ? QStringLiteral("/etc/fstab") : QFile::decodeName(other);
    QStringList points = mountFieldOf(table, 1);
    points.removeAll(QStringLiteral("/"));
    points.removeDuplicates();
    return points;
}

QSet<QString> mountedNow()
{
    const QByteArray other = qgetenv("LINUXBEE_MOUNTINFO");
    const QString table
        = other.isEmpty() ? QStringLiteral("/proc/self/mountinfo") : QFile::decodeName(other);
    const QStringList points = mountFieldOf(table, 4);
    return QSet<QString>(points.cbegin(), points.cend());
}

}
