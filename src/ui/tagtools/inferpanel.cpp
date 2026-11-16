#include "tagtools/inferpanel.h"

#include <QCheckBox>
#include <QLineEdit>
#include <QFormLayout>
#include <QSettings>
#include <QVBoxLayout>

namespace linuxbee::ui {

struct InferPanel::Private {
    QLineEdit *pattern = nullptr;
    QCheckBox *blankOnly = nullptr;
};

namespace {

// TT-INFER: the pattern a new panel has, and the one a stored blank pattern means.
QString defaultPattern()
{
    return QStringLiteral("<Track#> - <Title>");
}

}

InferPanel::InferPanel(QWidget *parent)
    : ToolPanel(parent)
    , d(new Private)
{
    d->pattern = new QLineEdit(defaultPattern(), this);
    d->pattern->setObjectName(QStringLiteral("patternEdit"));

    d->blankOnly = new QCheckBox(tr("Only fill fields that are empty"), this);
    d->blankOnly->setObjectName(QStringLiteral("blankOnlyBox"));
    d->blankOnly->setChecked(true);

    auto *form = new QFormLayout;
    form->addRow(tr("Pattern"), d->pattern);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(d->blankOnly);

    connect(d->pattern, &QLineEdit::textChanged, this, [this] { emit optionsChanged(); });
    connect(d->blankOnly, &QCheckBox::toggled, this, [this] { emit optionsChanged(); });
}

InferPanel::~InferPanel()
{
    delete d;
}

QString InferPanel::title() const
{
    return tr("Infer Tags From Filename");
}

QString InferPanel::problem() const
{
    return validateInferPattern(pattern());
}

QList<TagChanges> InferPanel::changes(const QList<EditTarget> &targets) const
{
    // Each keystroke lands here for every track. The names are already in the targets,
    // so no file is opened; the pattern is checked once, not once per track.
    if (!problem().isEmpty())
        return QList<TagChanges>(targets.size());
    const QString text = pattern();
    const bool blank = blankFieldsOnly();
    QList<TagChanges> out;
    out.reserve(targets.size());
    for (const EditTarget &target : targets)
        out.append(inferTags(target.tags, target.path, text, blank));
    return out;
}

void InferPanel::load(QSettings &settings)
{
    // TT-INFER: an empty or blank stored pattern means the new panel's; any other is
    // shown as it is. A stored blankOnly other than `false` means ticked.
    const QString stored = settings.value(QStringLiteral("tagtools/infer/pattern")).toString();
    const bool blankOnly = settings.value(QStringLiteral("tagtools/infer/blankOnly")).toString()
        != QLatin1String("false");
    d->pattern->setText(stored.trimmed().isEmpty() ? defaultPattern() : stored);
    d->blankOnly->setChecked(blankOnly);
}

void InferPanel::save(QSettings &settings) const
{
    settings.setValue(QStringLiteral("tagtools/infer/pattern"), pattern());
    settings.setValue(QStringLiteral("tagtools/infer/blankOnly"), blankFieldsOnly());
}

QString InferPanel::pattern() const
{
    return d->pattern->text();
}

bool InferPanel::blankFieldsOnly() const
{
    return d->blankOnly->isChecked();
}

}
