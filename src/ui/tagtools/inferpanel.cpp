#include "tagtools/inferpanel.h"

// Stub from the seed: the builder replaces this file. Contract: docs/spec/tag-tools-dialog.md.
namespace linuxbee::ui {

struct InferPanel::Private { };

InferPanel::InferPanel(QWidget *parent)
    : ToolPanel(parent)
    , d(new Private)
{
}

InferPanel::~InferPanel()
{
    delete d;
}

QString InferPanel::title() const
{
    return QString();
}

QString InferPanel::problem() const
{
    return QString();
}

QList<TagChanges> InferPanel::changes(const QList<EditTarget> &targets) const
{
    return QList<TagChanges>(targets.size());
}

void InferPanel::load(QSettings &)
{
}

void InferPanel::save(QSettings &) const
{
}

QString InferPanel::pattern() const
{
    return QString();
}

bool InferPanel::blankFieldsOnly() const
{
    return false;
}

}
