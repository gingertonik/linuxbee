#include "convert/convertdialog.h"

// Stub. Plan 13b replaces every body in this file.
namespace linuxbee::ui {

struct ConvertDialog::Private {
    ConvertBatch *batch = nullptr;
};

ConvertDialog::ConvertDialog(const QStringList &files, QSettings *settings, QWidget *parent)
    : QDialog(parent)
    , d(new Private)
{
    Q_UNUSED(files);
    Q_UNUSED(settings);
    d->batch = new ConvertBatch(this);
}

ConvertDialog::~ConvertDialog()
{
    delete d;
}

BatchOptions ConvertDialog::options() const
{
    return { };
}

QList<ConvertJob> ConvertDialog::plan() const
{
    return { };
}

ConvertBatch *ConvertDialog::batch() const
{
    return d->batch;
}

bool ConvertDialog::run()
{
    return false;
}

void ConvertDialog::reject()
{
    QDialog::reject();
}

}
