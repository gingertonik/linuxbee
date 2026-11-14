#include "convert/convertbatch.h"

#include <QMetaObject>
#include <QThread>
#include <QThreadPool>

#include <utility>

namespace linuxbee {

struct ConvertBatch::Private {
    QThreadPool pool;
    Converter converter;
    int maxParallel = 1;
    bool running = false;
    QList<ConvertJob> jobs;
    QList<ConvertResult> results;
    int remaining = 0;
};

ConvertBatch::ConvertBatch(QObject *parent)
    : QObject(parent)
    , d(new Private)
{
    qRegisterMetaType<ConvertResult>();
    d->converter = [](const ConvertJob &job, const ConvertProgress &progress) {
        return convertFile(job, progress);
    };
    d->maxParallel = defaultParallel();
}

ConvertBatch::~ConvertBatch()
{
    d->pool.waitForDone();
    delete d;
}

void ConvertBatch::setConverter(Converter converter)
{
    if (d->running)
        return;
    d->converter = std::move(converter);
}

int ConvertBatch::defaultParallel()
{
    return qBound(1, QThread::idealThreadCount(), 4);
}

int ConvertBatch::maxParallel() const
{
    return d->maxParallel;
}

void ConvertBatch::setMaxParallel(int count)
{
    if (d->running)
        return;
    d->maxParallel = qBound(1, count, 16);
}

bool ConvertBatch::start(const QList<ConvertJob> &jobs)
{
    if (d->running || jobs.isEmpty())
        return false;

    d->running = true;
    d->jobs = jobs;
    d->remaining = jobs.size();
    // Until a job reports, its result is Failed with an empty message.
    d->results.clear();
    d->results.resize(jobs.size());
    d->pool.setMaxThreadCount(d->maxParallel);

    for (int i = 0; i < jobs.size(); ++i) {
        // Runs on a pool thread. It touches only its own copies and posts back.
        d->pool.start([this, i, job = jobs.at(i), convert = d->converter]() {
            QMetaObject::invokeMethod(
                this, [this, i] { emit jobStarted(i); }, Qt::QueuedConnection);

            ConvertProgress progress = [this, i, last = 0.0](double fraction) mutable {
                if (fraction < last)
                    fraction = last;
                last = qBound(0.0, fraction, 1.0);
                const double shown = last;
                QMetaObject::invokeMethod(
                    this, [this, i, shown] { emit jobProgress(i, shown); }, Qt::QueuedConnection);
                return true;
            };
            const ConvertResult result = convert(job, progress);

            QMetaObject::invokeMethod(
                this,
                [this, i, result] {
                    d->results[i] = result;
                    emit jobFinished(i, result);
                    if (--d->remaining == 0) {
                        d->pool.waitForDone();
                        d->running = false;
                        emit finished();
                    }
                },
                Qt::QueuedConnection);
        });
    }
    return true;
}

void ConvertBatch::cancel()
{
}

bool ConvertBatch::isRunning() const
{
    return d->running;
}

int ConvertBatch::jobCount() const
{
    return d->jobs.size();
}

QList<ConvertResult> ConvertBatch::results() const
{
    return d->results;
}

}
