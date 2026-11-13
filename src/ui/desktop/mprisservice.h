#pragma once
#include <QDBusConnection>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <functional>

#include "playback/player.h"
#include "playback/playqueue.h"

// linuxBee as the desktop sees it: the MPRIS interface that media keys,
// the panel's player widget, lock screens and phone remotes talk to.
// Contract: docs/spec/desktop-mpris.md (MP-x). It only passes requests to
// the player and the queue and reports their state.
namespace linuxbee::ui {

class MprisService : public QObject {
    Q_OBJECT
public:
    struct Track {
        QString title;
        QStringList artists;
        QString album;
    };
    struct Hooks {
        std::function<void()> raise; // bring the window to the front
        std::function<void()> quit;
        std::function<Track(const QString &path)> describe; // what is known of a file
    };

    // Neither pointer is owned; both must outlive the service.
    MprisService(Player *player, PlayQueue *queue, Hooks hooks, QObject *parent = nullptr);
    ~MprisService() override;

    static QString serviceName(); // "org.mpris.MediaPlayer2.linuxbee"
    static QString objectPath(); // "/org/mpris/MediaPlayer2"

    // MP-START: false, with *error set, when the bus cannot be used or the name is taken.
    bool start(const QDBusConnection &connection, QString *error = nullptr);
    void stop();
    bool isRunning() const;

    QVariantMap metadata() const; // MP-METADATA, as sent

private:
    struct Private;
    Private *d;
};

}
