#include "tagtools/removetagspanel.h"

#include <QCheckBox>
#include <QListWidget>
#include <QLocale>
#include <QListWidgetItem>
#include <QSettings>
#include <QVBoxLayout>

namespace linuxbee::ui {

struct RemoveTagsPanel::Private {
    QListWidget *fields = nullptr;
    QCheckBox *allTags = nullptr;
    QCheckBox *pictures = nullptr;
};

RemoveTagsPanel::RemoveTagsPanel(QWidget *parent)
    : ToolPanel(parent)
    , d(new Private)
{
    d->fields = new QListWidget(this);
    d->fields->setObjectName(QStringLiteral("fieldList"));
    // TT-REMOVE: the catalogue fields in catalogue order, without Rating; none ticked.
    QStringList names = tagFieldNames();
    names.removeAll(QStringLiteral("Rating"));
    for (const QString &name : names) {
        auto *item = new QListWidgetItem(name, d->fields);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
    }

    d->allTags = new QCheckBox(tr("All tags"), this);
    d->allTags->setObjectName(QStringLiteral("allTagsBox"));
    d->pictures = new QCheckBox(tr("Embedded pictures"), this);
    d->pictures->setObjectName(QStringLiteral("picturesBox"));

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(d->fields);
    layout->addWidget(d->allTags);
    layout->addWidget(d->pictures);

    auto changed = [this] { emit optionsChanged(); };
    connect(d->fields, &QListWidget::itemChanged, this, changed);
    connect(d->allTags, &QCheckBox::toggled, this, [this] {
        d->fields->setEnabled(!d->allTags->isChecked());
        emit optionsChanged();
    });
    connect(d->pictures, &QCheckBox::toggled, this, changed);
}

RemoveTagsPanel::~RemoveTagsPanel()
{
    delete d;
}

QString RemoveTagsPanel::title() const
{
    return tr("Remove Tags");
}

QString RemoveTagsPanel::problem() const
{
    const RemoveOptions o = options();
    if (o.fields.isEmpty() && !o.allTags && !o.removePictures)
        return tr("Choose what to remove");
    return QString();
}

QList<TagChanges> RemoveTagsPanel::changes(const QList<EditTarget> &targets) const
{
    if (!problem().isEmpty())
        return QList<TagChanges>(targets.size());
    const RemoveOptions o = options();
    QList<TagChanges> out;
    for (const EditTarget &target : targets)
        out.append(removeTags(target.tags, o));
    return out;
}

void RemoveTagsPanel::load(QSettings &)
{
    // TT-REMOVE: nothing is remembered, so that what was removed from one selection
    // is never ticked already for the next.
}

void RemoveTagsPanel::save(QSettings &) const
{
}

QString RemoveTagsPanel::warning(int tracks) const
{
    // TT-REMOVE, the warning: only "All tags" and "Embedded pictures" ask.
    const bool all = d->allTags->isChecked();
    const bool pictures = d->pictures->isChecked();
    if (!all && !pictures)
        return QString();
    const QString count = tracks == 1
        ? tr("1 file")
        : tr("%1 files").arg(QLocale(QLocale::English).toString(tracks));
    if (all && pictures)
        return tr("Remove every tag and the embedded pictures from %1? The rating and tags that "
                  "are not in the list go too. This cannot be undone.")
            .arg(count);
    if (all)
        return tr("Remove every tag from %1? The rating and tags that are not in the list go "
                  "too. Embedded pictures stay. This cannot be undone.")
            .arg(count);
    return tr("Remove the embedded pictures from %1? This cannot be undone.").arg(count);
}

RemoveOptions RemoveTagsPanel::options() const
{
    RemoveOptions o;
    for (int i = 0; i < d->fields->count(); ++i) {
        const QListWidgetItem *item = d->fields->item(i);
        if (item->checkState() == Qt::Checked)
            o.fields.append(item->text());
    }
    o.allTags = d->allTags->isChecked();
    o.removePictures = d->pictures->isChecked();
    return o;
}

}
