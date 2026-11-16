#include "shell/scanprogress.h"

// Stub from the seed: the builder replaces this file. Contract: docs/spec/scan-runner.md (SP-x).
namespace linuxbee::ui {

struct ScanProgress::Private { };

ScanProgress::ScanProgress(MainWindow *, ActionRegistry *, Hooks, QObject *parent)
    : QObject(parent)
    , d(new Private)
{
}

ScanProgress::~ScanProgress()
{
    delete d;
}

void ScanProgress::watch(ScanRunner *)
{
}

bool ScanProgress::isShown() const
{
    return false;
}

bool ScanProgress::stop()
{
    return false;
}

int ScanProgress::updates() const
{
    return 0;
}

}
