#include "tagtools/tagtooldialog.h"

#include <QAbstractItemView>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <functional>

namespace linuxbee::ui {

namespace {

QString joined(const QStringList &values)
{
    return values.join(QStringLiteral("; "));
}

// TT-PREVIEW: the rows one track's changes show, fields first, then the other
// keys, then the pictures. Each row is File, Field, Now, Becomes.
QList<QStringList> rowsFor(const EditTarget &target, const TagChanges &changes)
{
    QList<QStringList> rows;
    const QString file = QFileInfo(target.path).fileName();
    for (auto it = changes.fields.constBegin(); it != changes.fields.constEnd(); ++it)
        rows.append(
            { file, it.key(), joined(target.tags.fields.value(it.key())), joined(it.value()) });
    for (auto it = changes.other.constBegin(); it != changes.other.constEnd(); ++it)
        rows.append({ file, QStringLiteral("other ") + it.key(),
            joined(target.tags.other.value(it.key())), joined(it.value()) });
    if (changes.pictures.has_value())
        rows.append(
            { file, QStringLiteral("Pictures"), QString::number(target.tags.pictures.size()),
                QString::number(changes.pictures->size()) });
    return rows;
}

// TT-APPLY, step 4: what a saved track now says, so the next preview shows only
// what is left. An empty list removes the field or key, as the saver does.
void applySaved(TrackTags &tags, const TagChanges &changes)
{
    for (auto it = changes.fields.constBegin(); it != changes.fields.constEnd(); ++it) {
        if (it.value().isEmpty())
            tags.fields.remove(it.key());
        else
            tags.fields.insert(it.key(), it.value());
    }
    for (auto it = changes.other.constBegin(); it != changes.other.constEnd(); ++it) {
        if (it.value().isEmpty())
            tags.other.remove(it.key());
        else
            tags.other.insert(it.key(), it.value());
    }
    if (changes.pictures.has_value())
        tags.pictures = *changes.pictures;
}

}

struct TagToolDialog::Private {
    ToolPanel *panel = nullptr;
    QList<EditTarget> targets;
    QSettings *settings = nullptr;
    TagToolDialog::Saver saver;
    TagToolDialog::Confirm confirm;
    QList<TagChanges> pending;
    int changedTracks = 0;
    QString problem;
    QTableWidget *table = nullptr;
    QLabel *summary = nullptr;
    QPushButton *applyButton = nullptr;
    QPushButton *cancelButton = nullptr;
    // Works out the preview again from the panel and the targets as they are now.
    std::function<void()> refresh;
};

TagToolDialog::TagToolDialog(
    ToolPanel *panel, const QList<EditTarget> &targets, QSettings *settings, QWidget *parent)
    : QDialog(parent)
    , d(new Private)
{
    // The panel must not be null: the dialog is built around it.
    setObjectName(QStringLiteral("tagToolDialog"));
    d->panel = panel;
    d->targets = targets;
    d->settings = settings;
    d->saver = [](const QString &path, const TagChanges &changes) {
        return linuxbee::writeTags(path, changes);
    };
    panel->setParent(this);
    setWindowTitle(panel->title());

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(panel);

    d->table = new QTableWidget(0, 4, this);
    d->table->setObjectName(QStringLiteral("previewTable"));
    d->table->setHorizontalHeaderLabels({ tr("File"), tr("Field"), tr("Now"), tr("Becomes") });
    d->table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    d->table->verticalHeader()->setVisible(false);
    layout->addWidget(d->table);

    d->summary = new QLabel(this);
    d->summary->setObjectName(QStringLiteral("summaryLabel"));
    layout->addWidget(d->summary);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    d->applyButton = new QPushButton(tr("Apply"), this);
    d->applyButton->setObjectName(QStringLiteral("applyButton"));
    d->applyButton->setDefault(true);
    d->cancelButton = new QPushButton(tr("Cancel"), this);
    d->cancelButton->setObjectName(QStringLiteral("cancelButton"));
    buttons->addWidget(d->applyButton);
    buttons->addWidget(d->cancelButton);
    layout->addLayout(buttons);

    d->refresh = [this] {
        d->table->setRowCount(0);
        d->problem = d->panel->problem();
        if (!d->problem.isEmpty()) {
            d->pending = QList<TagChanges>(d->targets.size());
            d->changedTracks = 0;
            d->summary->setText(d->problem);
            d->applyButton->setEnabled(false);
            return;
        }

        d->pending = d->panel->changes(d->targets);
        d->pending.resize(d->targets.size());

        QList<QStringList> rows;
        d->changedTracks = 0;
        for (int i = 0; i < d->targets.size(); ++i) {
            if (d->pending.at(i).isEmpty())
                continue;
            ++d->changedTracks;
            rows.append(rowsFor(d->targets.at(i), d->pending.at(i)));
        }

        d->table->setRowCount(rows.size());
        for (int r = 0; r < rows.size(); ++r) {
            for (int c = 0; c < rows.at(r).size(); ++c) {
                auto *item = new QTableWidgetItem(rows.at(r).at(c));
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                d->table->setItem(r, c, item);
            }
        }

        if (d->changedTracks == 0) {
            d->summary->setText(tr("Nothing to change"));
            d->applyButton->setEnabled(false);
        } else {
            d->summary->setText(
                tr("%1 of %2 tracks would change").arg(d->changedTracks).arg(d->targets.size()));
            d->applyButton->setEnabled(true);
        }
    };

    connect(panel, &ToolPanel::optionsChanged, this, [this] { d->refresh(); });
    connect(d->applyButton, &QPushButton::clicked, this, [this] { apply(); });
    connect(d->cancelButton, &QPushButton::clicked, this, &QDialog::reject);

    // TT-REMEMBER: the stored options are loaded before the first preview.
    if (settings)
        panel->load(*settings);
    d->refresh();
}

TagToolDialog::~TagToolDialog()
{
    delete d;
}

void TagToolDialog::setSaver(Saver saver)
{
    d->saver = std::move(saver);
}

void TagToolDialog::setConfirm(Confirm confirm)
{
    d->confirm = std::move(confirm);
}

ToolPanel *TagToolDialog::panel() const
{
    return d->panel;
}

QList<TagChanges> TagToolDialog::pendingChanges() const
{
    return d->pending;
}

bool TagToolDialog::apply()
{
    if (!d->problem.isEmpty() || d->changedTracks == 0)
        return false;

    // TT-WARN: once per Apply, before the first file is handed to the saver.
    const QString question = d->panel->warning(d->changedTracks);
    if (!question.isEmpty()) {
        const bool yes = d->confirm ? d->confirm(question)
                                    : QMessageBox::question(this, windowTitle(), question,
                                          QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                == QMessageBox::Yes;
        if (!yes)
            return false;
    }

    // TT-APPLY, step 1: every track with changes goes to the saver, in order.
    // A failure does not stop the others.
    QList<qint64> written;
    QStringList failedNames;
    for (int i = 0; i < d->targets.size(); ++i) {
        const TagChanges changes = d->pending.at(i);
        if (changes.isEmpty())
            continue;
        EditTarget &target = d->targets[i];
        const WriteResult result = d->saver(target.path, changes);
        if (!result.ok()) {
            QString name = QFileInfo(target.path).fileName();
            // WS-LINK, WS-SAME: a file that was refused says why, as the tag editor does.
            if ((result.status == WriteResult::Status::HardLinked
                    || result.status == WriteResult::Status::ChangedOnDisk)
                && !result.problems.isEmpty())
                name += QStringLiteral(" (") + result.problems.first() + QLatin1Char(')');
            failedNames.append(name);
            continue;
        }
        applySaved(target.tags, changes);
        if (result.status == WriteResult::Status::Ok)
            written.append(target.id);
    }

    // TT-APPLY, step 2.
    if (!written.isEmpty())
        emit saved(written);

    // TT-APPLY, step 3: a complete success stores the options and closes.
    if (failedNames.isEmpty()) {
        if (d->settings) {
            d->panel->save(*d->settings);
            d->settings->sync();
        }
        accept();
        return true;
    }

    // TT-APPLY, step 4: the dialog stays open on what is still to do.
    d->refresh();
    d->summary->setText(tr("Could not save %1 of %2: %3")
            .arg(failedNames.size())
            .arg(d->targets.size())
            .arg(failedNames.join(QStringLiteral(", "))));
    return false;
}

}
