#include "tagtools/renumberpanel.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

namespace linuxbee::ui {

struct RenumberPanel::Private {
    QSpinBox *start = nullptr;
    QSpinBox *digits = nullptr;
    QCheckBox *total = nullptr;
};

RenumberPanel::RenumberPanel(QWidget *parent)
    : ToolPanel(parent)
    , d(new Private)
{
    d->start = new QSpinBox(this);
    d->start->setObjectName(QStringLiteral("startSpin"));
    d->start->setRange(0, 99999);
    d->start->setValue(1);

    d->digits = new QSpinBox(this);
    d->digits->setObjectName(QStringLiteral("digitsSpin"));
    d->digits->setRange(0, 9);
    d->digits->setValue(0);

    d->total = new QCheckBox(tr("Also write the track count"), this);
    d->total->setObjectName(QStringLiteral("totalBox"));

    auto *form = new QFormLayout;
    form->addRow(tr("Start at"), d->start);
    form->addRow(tr("Digits"), d->digits);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(d->total);

    connect(d->start, &QSpinBox::valueChanged, this, [this] { emit optionsChanged(); });
    connect(d->digits, &QSpinBox::valueChanged, this, [this] { emit optionsChanged(); });
    connect(d->total, &QCheckBox::toggled, this, [this] { emit optionsChanged(); });
}

RenumberPanel::~RenumberPanel()
{
    delete d;
}

QString RenumberPanel::title() const
{
    return tr("Renumber Tracks");
}

QString RenumberPanel::problem() const
{
    return QString(); // TT-RENUMBER: there is never a problem
}

QList<TagChanges> RenumberPanel::changes(const QList<EditTarget> &targets) const
{
    // The targets in the order given: the caller decides the order.
    QList<TrackTags> tags;
    for (const EditTarget &target : targets)
        tags.append(target.tags);
    return renumberTracks(tags, options());
}

void RenumberPanel::load(QSettings &settings)
{
    // TT-RENUMBER: a stored digits that is not a whole number from 0 to 9 means 0;
    // a stored total other than `true` means no tick. The start is never read.
    bool ok = false;
    int digits = settings.value(QStringLiteral("tagtools/renumber/digits")).toString().toInt(&ok);
    if (!ok || digits < 0 || digits > 9)
        digits = 0;
    const bool total = settings.value(QStringLiteral("tagtools/renumber/total")).toString()
        == QLatin1String("true");
    d->digits->setValue(digits);
    d->total->setChecked(total);
}

void RenumberPanel::save(QSettings &settings) const
{
    settings.setValue(QStringLiteral("tagtools/renumber/digits"), d->digits->value());
    settings.setValue(QStringLiteral("tagtools/renumber/total"), d->total->isChecked());
}

RenumberOptions RenumberPanel::options() const
{
    RenumberOptions o;
    o.start = d->start->value();
    o.digits = d->digits->value();
    o.writeTotal = d->total->isChecked();
    return o;
}

}
