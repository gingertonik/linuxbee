#include "shell/scanrunner.h"

// STUB (plan 36b). The builder writes the real bodies here.
namespace linuxbee::ui {

struct ScanRunner::Private { };

ScanRunner::ScanRunner(Library *, ScanRunnerHooks, QObject *parent)
    : QObject(parent)
    , d(nullptr)
{
}

ScanRunner::~ScanRunner() = default;

bool ScanRunner::isRunning() const
{
    return false;
}

int ScanRunner::waiting() const
{
    return 0;
}

int ScanRunner::partSize() const
{
    return 0;
}

void ScanRunner::setPartSize(int)
{
}

void ScanRunner::scan(const QStringList &, const ScanOptions &)
{
}

void ScanRunner::cancel()
{
}

}
