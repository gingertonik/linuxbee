#include "library/setaside.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QSet>

#include <ctime>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "organise/transfer.h"

namespace linuxbee::setaside {
namespace {

// The age, in nanoseconds, that a file must be older than: LINUXBEE_LEFTOVER_AGE seconds, or 600.
qint64 leftoverAgeNs()
{
    bool ok = false;
    const qint64 seconds = qEnvironmentVariable("LINUXBEE_LEFTOVER_AGE").toLongLong(&ok);
    return (ok && seconds >= 0 ? seconds : 600) * 1000000000LL;
}

qint64 nowNs()
{
    timespec now { };
    ::clock_gettime(CLOCK_REALTIME, &now);
    return qint64(now.tv_sec) * 1000000000LL + now.tv_nsec;
}

bool olderThan(const struct stat &info, qint64 ageNs)
{
    const qint64 changed = qint64(info.st_ctim.tv_sec) * 1000000000LL + info.st_ctim.tv_nsec;
    return nowNs() - changed > ageNs;
}

// `.linuxbee-replaced-N`, N from 1 to 9999 with no leading zero, as execute.cpp writes it.
bool isWaitingName(const QByteArray &name)
{
    static const QByteArray prefix(".linuxbee-replaced-");
    if (!name.startsWith(prefix))
        return false;
    const QByteArray digits = name.mid(prefix.size());
    if (digits.isEmpty() || digits.size() > 4 || digits.at(0) == '0')
        return false;
    for (const char c : digits) {
        if (c < '0' || c > '9')
            return false;
    }
    return true;
}

struct Entry {
    QByteArray name;
    struct stat info { };
};

// The entries of a folder, each looked at without following a link. An unreadable folder gives
// nothing.
QList<Entry> entriesOf(const QByteArray &folder)
{
    QList<Entry> found;
    DIR *dir = ::opendir(folder.constData());
    if (!dir)
        return found;
    while (const dirent *item = ::readdir(dir)) {
        const QByteArray name(item->d_name);
        if (name == "." || name == "..")
            continue;
        Entry entry;
        entry.name = name;
        if (::fstatat(::dirfd(dir), item->d_name, &entry.info, AT_SYMLINK_NOFOLLOW) == 0)
            found.append(entry);
    }
    ::closedir(dir);
    return found;
}

struct Walk {
    qint64 ageNs = 0;
    QSet<QString> putBack;
    QSet<QString> kept;

    void waiting(const QByteArray &folder)
    {
        const QByteArray above = folder.left(folder.lastIndexOf('/'));
        bool tookOne = false;
        for (const Entry &entry : entriesOf(folder)) {
            // Regular files only: a link, a folder or anything else is not ours.
            if (!S_ISREG(entry.info.st_mode) || !olderThan(entry.info, ageNs))
                continue;
            const QString from = QFile::decodeName(folder + '/' + entry.name);
            const QString to = QFile::decodeName(above + '/' + entry.name);
            // Never over a name that is taken: the move of O-SPARE does not replace, and a link
            // that leads nowhere is taken for it.
            if (organise::transferFile(from, to, true).isEmpty()) {
                putBack.insert(to);
                tookOne = true;
            } else {
                kept.insert(from);
            }
        }
        struct stat own { };
        // A folder left empty goes, and rmdir refuses one that is not. One that nothing was
        // taken from is left while it is young: an Organise may be about to use it.
        if ((tookOne || (::lstat(folder.constData(), &own) == 0 && olderThan(own, ageNs))))
            ::rmdir(folder.constData());
    }

    void below(const QByteArray &folder)
    {
        for (const Entry &entry : entriesOf(folder)) {
            if (!S_ISDIR(entry.info.st_mode))
                continue; // a link is never followed
            const QByteArray path = folder + '/' + entry.name;
            if (isWaitingName(entry.name))
                waiting(path);
            else if (!entry.name.startsWith('.'))
                below(path);
        }
    }
};

}

Swept sweepSetAside(const QStringList &folders)
{
    Walk walk;
    walk.ageNs = leftoverAgeNs();
    QSet<QString> seen;
    for (const QString &folder : folders) {
        if (folder.isEmpty() || seen.contains(folder))
            continue;
        seen.insert(folder);
        walk.below(QFile::encodeName(folder));
    }
    Swept swept;
    swept.putBack = QStringList(walk.putBack.cbegin(), walk.putBack.cend());
    swept.kept = QStringList(walk.kept.cbegin(), walk.kept.cend());
    swept.putBack.sort();
    swept.kept.sort();
    return swept;
}

QString realPathOf(const QString &waitingPath)
{
    const QFileInfo file(waitingPath);
    return QFileInfo(file.absolutePath()).absolutePath() + QLatin1Char('/') + file.fileName();
}

}
