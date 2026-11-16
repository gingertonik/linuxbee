#include "skins/skinchoosing.h"

// Stub from the seed: the builder replaces this file. Contract: docs/spec/skins.md (SC-x).
namespace linuxbee::ui {

struct SkinChoosing::Private {
    QString folder;
};

SkinChoosing::SkinChoosing(MainWindow *, ActionRegistry *, SkinApplier *, const QString &folder,
    Hooks, QObject *parent)
    : QObject(parent)
    , d(new Private { folder })
{
}

SkinChoosing::~SkinChoosing()
{
    delete d;
}

QString SkinChoosing::folder() const
{
    return d->folder;
}

QStringList SkinChoosing::skinFiles() const
{
    return { };
}

bool SkinChoosing::choose()
{
    return false;
}

bool SkinChoosing::add()
{
    return false;
}

bool SkinChoosing::none()
{
    return false;
}

QString SkinChoosing::lastMessage() const
{
    return QString();
}

}
