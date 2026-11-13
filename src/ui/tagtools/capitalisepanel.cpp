#include "tagtools/capitalisepanel.h"

#include <QComboBox>
#include <QFormLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QSettings>
#include <QVBoxLayout>

namespace linuxbee::ui {

struct CapitalisePanel::Private {
    QComboBox *style = nullptr;
    QListWidget *fields = nullptr;
};

namespace {

// TT-CASE: the catalogue fields the tool offers, in catalogue order, without Rating.
QStringList offeredFields()
{
    QStringList names = tagFieldNames();
    names.removeAll(QStringLiteral("Rating"));
    return names;
}

// The item data the style combo stores for each style.
CaseStyle styleFromId(const QString &id)
{
    if (id == QStringLiteral("title"))
        return CaseStyle::TitleCase;
    if (id == QStringLiteral("sentence"))
        return CaseStyle::Sentence;
    if (id == QStringLiteral("upper"))
        return CaseStyle::Upper;
    if (id == QStringLiteral("lower"))
        return CaseStyle::Lower;
    return CaseStyle::EachWord;
}

// TT-CASE: the rows ticked when nothing was stored, or none of the stored names is a row.
const QStringList &defaultTicked()
{
    static const QStringList names { QStringLiteral("Title"), QStringLiteral("Artist"),
        QStringLiteral("Album"), QStringLiteral("Album Artist") };
    return names;
}

}

CapitalisePanel::CapitalisePanel(QWidget *parent)
    : ToolPanel(parent)
    , d(new Private)
{
    d->style = new QComboBox(this);
    d->style->setObjectName(QStringLiteral("styleCombo"));
    d->style->addItem(tr("Each Word"), QStringLiteral("each-word"));
    d->style->addItem(tr("Title Case"), QStringLiteral("title"));
    d->style->addItem(tr("Sentence case"), QStringLiteral("sentence"));
    d->style->addItem(tr("UPPER CASE"), QStringLiteral("upper"));
    d->style->addItem(tr("lower case"), QStringLiteral("lower"));

    d->fields = new QListWidget(this);
    d->fields->setObjectName(QStringLiteral("fieldList"));
    const QStringList ticked = defaultTicked();
    for (const QString &name : offeredFields()) {
        auto *item = new QListWidgetItem(name, d->fields);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(ticked.contains(name) ? Qt::Checked : Qt::Unchecked);
    }

    auto *form = new QFormLayout;
    form->addRow(tr("Style"), d->style);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(d->fields);

    auto changed = [this] { emit optionsChanged(); };
    connect(d->style, &QComboBox::currentIndexChanged, this, changed);
    connect(d->fields, &QListWidget::itemChanged, this, changed);
}

CapitalisePanel::~CapitalisePanel()
{
    delete d;
}

QString CapitalisePanel::title() const
{
    return tr("Capitalise");
}

QString CapitalisePanel::problem() const
{
    // An empty field list would mean the default four to the core, so an empty
    // list is refused here instead.
    if (options().fields.isEmpty())
        return tr("Choose at least one field");
    return QString();
}

QList<TagChanges> CapitalisePanel::changes(const QList<EditTarget> &targets) const
{
    if (!problem().isEmpty())
        return QList<TagChanges>(targets.size());
    const CaseOptions o = options();
    QList<TagChanges> out;
    for (const EditTarget &target : targets)
        out.append(capitalise(target.tags, o));
    return out;
}

void CapitalisePanel::load(QSettings &settings)
{
    // TT-CASE: an unknown style means Each Word. A stored name that is not a row
    // is ignored; when none is a row, the four defaults are ticked.
    const int index
        = d->style->findData(settings.value(QStringLiteral("tagtools/case/style")).toString());
    d->style->setCurrentIndex(index >= 0 ? index : 0);

    const QStringList stored
        = settings.value(QStringLiteral("tagtools/case/fields")).toStringList();
    const QStringList rows = offeredFields();
    bool anyRow = false;
    for (const QString &name : stored)
        anyRow = anyRow || rows.contains(name);
    const QStringList wanted = anyRow ? stored : defaultTicked();
    for (int i = 0; i < d->fields->count(); ++i) {
        QListWidgetItem *item = d->fields->item(i);
        item->setCheckState(wanted.contains(item->text()) ? Qt::Checked : Qt::Unchecked);
    }
}

void CapitalisePanel::save(QSettings &settings) const
{
    settings.setValue(QStringLiteral("tagtools/case/style"), d->style->currentData().toString());
    settings.setValue(QStringLiteral("tagtools/case/fields"), options().fields);
}

CaseOptions CapitalisePanel::options() const
{
    CaseOptions o;
    o.style = styleFromId(d->style->currentData().toString());
    for (int i = 0; i < d->fields->count(); ++i) {
        const QListWidgetItem *item = d->fields->item(i);
        if (item->checkState() == Qt::Checked)
            o.fields.append(item->text());
    }
    return o;
}

}
