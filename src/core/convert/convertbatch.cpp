#include "convert/convertbatch.h"

// Stub. Plan 08b replaces every body in this file.
namespace linuxbee {

struct ConvertBatch::Private { };

ConvertBatch::ConvertBatch(QObject *parent)
    : QObject(parent)
    , d(nullptr)
{
}

ConvertBatch::~ConvertBatch()
{
    Q_UNUSED(d);
}

void ConvertBatch::setConverter(Converter converter)
{
    Q_UNUSED(converter);
}

int ConvertBatch::defaultParallel()
{
    return 0;
}

int ConvertBatch::maxParallel() const
{
    return 0;
}

void ConvertBatch::setMaxParallel(int count)
{
    Q_UNUSED(count);
}

bool ConvertBatch::start(const QList<ConvertJob> &jobs)
{
    Q_UNUSED(jobs);
    return false;
}

void ConvertBatch::cancel()
{
}

bool ConvertBatch::isRunning() const
{
    return false;
}

int ConvertBatch::jobCount() const
{
    return 0;
}

QList<ConvertResult> ConvertBatch::results() const
{
    return { };
}

}
