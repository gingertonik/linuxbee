#pragma once
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

#include "convert/converter.h"

// Converts many files: works out where each result goes, then runs the jobs
// a few at a time in the background and reports as they finish.
// Contract: docs/spec/convert-batch.md (B-x).
namespace linuxbee {

struct BatchOptions {
    // Empty: each result goes next to its source.
    QString outputFolder;
    EncoderSettings settings;
    ConvertJob::IfExists ifExists = ConvertJob::IfExists::Skip;
    bool copyTags = true;
    bool copyPictures = true;
    WriteOptions tagOptions;
    QString ffmpegPath = QStringLiteral("ffmpeg");
};

// B-DEST: one job per source, in order. Pure: the disk is not looked at.
QList<ConvertJob> planBatch(const QStringList &sources, const BatchOptions &options);

class ConvertBatch : public QObject {
    Q_OBJECT
public:
    using Converter = std::function<ConvertResult(const ConvertJob &, const ConvertProgress &)>;

    explicit ConvertBatch(QObject *parent = nullptr);
    // Stops a run that is under way and waits for it.
    ~ConvertBatch() override;

    // By default linuxbee::convertFile. Called on background threads.
    void setConverter(Converter converter);

    // B-PARALLEL: how many jobs run at once, 1 to 16.
    static int defaultParallel();
    int maxParallel() const;
    void setMaxParallel(int count);

    // B-RUN. False, starting nothing, when a run is under way or `jobs` is empty.
    bool start(const QList<ConvertJob> &jobs);
    // B-CANCEL
    void cancel();
    bool isRunning() const;

    int jobCount() const;
    // One per job, by index. A job that has not finished has status Failed
    // and an empty message.
    QList<ConvertResult> results() const;

signals:
    // All four are emitted on this object's thread.
    void jobStarted(int index);
    void jobProgress(int index, double fraction);
    void jobFinished(int index, const linuxbee::ConvertResult &result);
    void finished();

private:
    struct Private;
    Private *d;
};

}
