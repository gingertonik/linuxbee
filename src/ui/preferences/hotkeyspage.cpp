#include "preferences/hotkeyspage.h"

#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "preferences/hotkeyspage_p.h"

// The Hotkeys page: its widgets, its table and its filter. The edits
// themselves live in hotkeyspage_edit.cpp.
// Contract: docs/spec/preferences.md (HK-TABLE, HK-FILTER, HK-BUTTONS).
namespace linuxbee::ui {

HotkeysPage::HotkeysPage(ActionRegistry *actions, QSettings *settings, QWidget *parent)
    : PreferencesPage(parent)
    , d(new Private)
{
    d->actions = actions;
    d->settings = settings;

    d->filterEdit = new QLineEdit(this);
    d->filterEdit->setObjectName(QStringLiteral("filterEdit"));
    d->filterEdit->setPlaceholderText(tr("Search commands and keys"));

    d->table = new QTableWidget(this);
    d->table->setObjectName(QStringLiteral("hotkeyTable"));
    d->table->setColumnCount(2);
    d->table->setHorizontalHeaderLabels({ tr("Command"), tr("Hotkey") });
    d->table->setSelectionBehavior(QAbstractItemView::SelectRows);
    d->table->setSelectionMode(QAbstractItemView::SingleSelection);
    d->table->setEditTriggers(QAbstractItemView::NoEditTriggers); // HK-TABLE

    // One row per command of the registry, in catalogue order (HK-TABLE).
    const QStringList ids = d->actions->ids();
    d->table->setRowCount(ids.size());
    for (int r = 0; r < ids.size(); ++r) {
        auto *command = new QTableWidgetItem(commandLabel(d->actions->info(ids[r])));
        command->setData(Qt::UserRole, ids[r]); // the id, not the row number
        command->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        d->table->setItem(r, 0, command);
        auto *key = new QTableWidgetItem(QString());
        key->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        d->table->setItem(r, 1, key);
        d->pending.insert(ids[r], QKeySequence());
    }

    d->keyEdit = new QKeySequenceEdit(this);
    d->keyEdit->setObjectName(QStringLiteral("keyEdit"));
    d->keyEdit->setMaximumSequenceLength(1); // HK-BUTTONS: one combination at most

    d->assignButton = new QPushButton(tr("Assign"), this);
    d->assignButton->setObjectName(QStringLiteral("assignButton"));
    d->assignButton->setEnabled(false); // HK-BUTTONS: the row buttons wait
    d->clearButton = new QPushButton(tr("Clear"), this);
    d->clearButton->setObjectName(QStringLiteral("clearButton"));
    d->clearButton->setEnabled(false);
    d->resetButton = new QPushButton(tr("Reset"), this);
    d->resetButton->setObjectName(QStringLiteral("resetButton"));
    d->resetButton->setEnabled(false);
    d->resetAllButton = new QPushButton(tr("Restore Defaults"), this);
    d->resetAllButton->setObjectName(QStringLiteral("resetAllButton"));

    d->conflictLabel = new QLabel(this);
    d->conflictLabel->setObjectName(QStringLiteral("conflictLabel"));

    auto *editRow = new QHBoxLayout;
    editRow->addWidget(d->keyEdit);
    editRow->addWidget(d->assignButton);
    editRow->addWidget(d->clearButton);
    editRow->addWidget(d->resetButton);
    editRow->addWidget(d->resetAllButton);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(d->filterEdit);
    layout->addWidget(d->table, 1);
    layout->addLayout(editRow);
    layout->addWidget(d->conflictLabel);

    load(); // HK-EDIT: the pending keys start as the registry's
}

HotkeysPage::~HotkeysPage()
{
    delete d;
}

QString HotkeysPage::title() const
{
    return tr("Hotkeys");
}

QString HotkeysPage::commandLabel(const ActionInfo &info)
{
    // HK-TABLE: menu path joined with " > ", the ampersands and a closing
    // ellipsis removed; a command in no menu shows its text alone.
    QString text = info.text;
    text.remove(QLatin1Char('&'));
    if (text.endsWith(QStringLiteral("\u2026")))
        text.chop(1);
    text = text.trimmed();
    if (info.menu.isEmpty())
        return text;
    QString path = info.menu;
    path.replace(QLatin1Char('/'), QStringLiteral(" > "));
    return path + QStringLiteral(" > ") + text;
}

int HotkeysPage::rowForId(const QString &id) const
{
    for (int r = 0; r < d->table->rowCount(); ++r) {
        const QTableWidgetItem *item = d->table->item(r, 0);
        if (item && item->data(Qt::UserRole).toString() == id)
            return r;
    }
    return -1;
}

QString HotkeysPage::idForRow(int row) const
{
    if (row < 0 || row >= d->table->rowCount())
        return { };
    const QTableWidgetItem *item = d->table->item(row, 0);
    return item ? item->data(Qt::UserRole).toString() : QString();
}

QString HotkeysPage::selectedId() const
{
    return idForRow(d->table->currentRow());
}

void HotkeysPage::setRowKey(int row, const QKeySequence &key)
{
    if (QTableWidgetItem *item = d->table->item(row, 1))
        item->setText(key.toString(QKeySequence::NativeText)); // HK-TABLE
}

void HotkeysPage::refreshHotkeyCells()
{
    for (auto it = d->pending.constBegin(); it != d->pending.constEnd(); ++it)
        setRowKey(rowForId(it.key()), it.value());
}

void HotkeysPage::applyFilter()
{
    // Task 6: HK-FILTER. Until then every row shows.
    for (int r = 0; r < d->table->rowCount(); ++r)
        d->table->setRowHidden(r, false);
}

}
