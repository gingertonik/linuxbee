#include "organise/organisedialog.h"

// Stub. Plan 13 replaces every body in this file.
namespace linuxbee::ui {

struct OrganiseDialog::Private { };

OrganiseDialog::OrganiseDialog(const QStringList &files, QSettings *settings, QWidget *parent)
    : QDialog(parent)
    , d(nullptr)
{
    Q_UNUSED(files);
    Q_UNUSED(settings);
}

OrganiseDialog::~OrganiseDialog()
{
    Q_UNUSED(d);
}

void OrganiseDialog::setPlanner(Planner planner)
{
    Q_UNUSED(planner);
}

void OrganiseDialog::setExecutor(Executor executor)
{
    Q_UNUSED(executor);
}

QString OrganiseDialog::templateText() const
{
    return { };
}

OrganiseOptions OrganiseDialog::options() const
{
    return { };
}

QList<PlannedMove> OrganiseDialog::preview() const
{
    return { };
}

bool OrganiseDialog::run()
{
    return false;
}

}
