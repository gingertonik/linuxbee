#pragma once
#include <QString>
#include <QStringList>

#include <memory>

class QLockFile;

// The profile: the folders the application keeps its files in, the files of an
// older build taken along, and the lock that lets one program use a profile at
// a time (docs/spec/main-window.md: W-PLACES, W-ADOPT, W-ONE).
namespace linuxbee::ui {

// W-PLACES: `$XDG_CONFIG_HOME/linuxbee` and `$XDG_DATA_HOME/linuxbee`. The
// application name is set and the organisation name is not, so the name
// appears once.
QString profileSettingsFolder();
QString profileDataFolder();

// W-ONE: made in the data folder before the library is opened, and let go when
// the program ends, however it ends (a dead owner is noticed by QLockFile).
class ProfileLock {
public:
    enum class State {
        Held, // this program has the profile
        Busy, // another program has it
        Failed, // the lock could not be made (no folder, no permission)
    };

    explicit ProfileLock(const QString &dataFolder);
    ~ProfileLock();
    ProfileLock(const ProfileLock &) = delete;
    ProfileLock &operator=(const ProfileLock &) = delete;

    State state() const { return m_state; }
    // For Busy and Failed: the one line for standard error, without the newline.
    QString message() const { return m_message; }

private:
    std::unique_ptr<QLockFile> m_lock;
    State m_state = State::Failed;
    QString m_message;
};

// W-ADOPT, "Open elsewhere": when another program of this user has a file under
// `<folder>/linuxbee` open (read from /proc/<pid>/fd), the line for standard error
// that says so (it contains `already running`); empty when none has. Ask for the
// settings folder and the data folder before anything is moved.
QString olderFilesInUse(const QString &folder);

// W-ADOPT: each entry of `<folder>/linuxbee` (the folder of a build that had the
// name twice) is renamed into `folder` when neither its name nor a companion's
// name of it (`-wal`, `-shm`, `-journal`) is there. A companion is never moved by
// itself. An entry with a companion beside it is settled first (opened as a
// database and closed, only if it begins as one) and moved only when no companion
// is left. Whatever is left is left whole. A folder is never merged and nothing
// is overwritten. The inner folder is removed once it is empty; one that is a
// link is not followed and not touched. Returns one line for standard error for
// each entry left behind. Call it under the lock, before the library is opened.
QStringList adoptOlderFiles(const QString &folder);

// W-ADOPT, "Never an empty library while an older one waits": after adoption,
// when `<dataFolder>/linuxbee/library.db` exists (through a link too) and
// `<dataFolder>/library.db` does not, the line for standard error (it contains
// `was not taken over`) that names both places; otherwise empty.
QString libraryNotTakenOver(const QString &dataFolder);

}
