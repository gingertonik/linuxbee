#pragma once
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <optional>
#include <random>

// The Playing Tracks list: what is playing, what comes next, and how
// shuffle and repeat choose. Contract: docs/spec/play-queue.md.
// It holds paths and plays nothing; the player asks it what is next.
namespace linuxbee {

class PlayQueue : public QObject {
    Q_OBJECT
public:
    enum class Repeat { Off, All, One };

    struct Entry {
        qint64 entryId = 0; // unique within this queue for as long as it lives
        QString path;
    };

    explicit PlayQueue(QObject *parent = nullptr);

    QList<Entry> entries() const;
    int count() const;
    int currentIndex() const; // -1: no current entry
    QString currentPath() const; // empty when there is none

    // Q-EDIT
    void playNow(const QStringList &paths, int startAt = 0);
    void queueNext(const QStringList &paths);
    void queueLast(const QStringList &paths);
    int removeAt(const QList<int> &positions); // returns how many were removed
    int removePaths(const QStringList &paths); // every entry with one of them, but the current one
    int renamePaths(const QHash<QString, QString> &oldToNew); // Q-RENAME
    bool move(int from, int to);
    void clear();
    bool setCurrent(int index);

    // Q-NEXT. Each returns the new current index, or -1 when there is nowhere
    // to go (the current entry then stays as it is). `automatic` is true when a
    // track ended by itself and false when the user asked.
    int next(bool automatic);
    int previous();
    bool hasNext(bool automatic) const;
    // Q-PEEK: the entry next(automatic) would make current. Changes nothing.
    std::optional<Entry> peekNext(bool automatic) const;

    Repeat repeat() const;
    void setRepeat(Repeat repeat);
    bool shuffle() const;
    void setShuffle(bool on, quint32 seed);

signals:
    void entriesChanged();
    void currentChanged(int index);
    void modeChanged();
    void nextChanged(); // Q-PEEK: peekNext(true) or peekNext(false) may now answer differently

private:
    void reshuffle();
    QList<qint64> dealRound(
        std::mt19937 &random) const; // every entry but the current one, shuffled
    void setCurrentInternal(int index);

    QList<Entry> m_entries;
    int m_current = -1;
    int m_resumeAt = 0; // Q-EDIT: where next() starts when there is no current entry
    qint64 m_nextId = 1;
    Repeat m_repeat = Repeat::Off;
    bool m_shuffle = false;
    std::mt19937 m_random;
    QList<qint64> m_upcoming; // shuffle: entry ids still to play, in order
    QList<qint64> m_history; // shuffle: entry ids played before the current one
};

// Q-COUNT: what a finished play does to the statistics.
struct PlayCountRule {
    double playedFraction = 0.5; // a play: more than this share of the track ...
    int playedSeconds = 240; // ... or more than this many seconds
    int skipSeconds = 20; // a skip: the user moved on before this many seconds
};
enum class PlayOutcome { Neither, Played, Skipped };
PlayOutcome classifyPlay(
    qint64 listenedMs, qint64 durationMs, bool endedByUser, const PlayCountRule &rule = { });

}
