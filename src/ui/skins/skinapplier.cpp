#include "skins/skinapplier.h"

// Stub. Plan 23 replaces every body in this file.
namespace linuxbee::ui {

struct SkinApplier::Private { };

SkinApplier::SkinApplier(QApplication *application, QSettings *settings, QObject *parent)
    : QObject(parent)
    , d(nullptr)
{
    Q_UNUSED(application);
    Q_UNUSED(settings);
}

SkinApplier::~SkinApplier()
{
    Q_UNUSED(d);
}

QString SkinApplier::currentFile() const
{
    return QStringLiteral("?");
}

QString SkinApplier::basis() const
{
    return QStringLiteral("?");
}

QString SkinApplier::lastError() const
{
    return { };
}

bool SkinApplier::apply(const QString &file)
{
    Q_UNUSED(file);
    return false;
}

void SkinApplier::clear()
{
}

}
