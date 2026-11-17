#include "tageditor/tageditordialog.h"

#include "tageditor/tageditordialog_p.h"
#include "tagedit/tagedit.h"

#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>

namespace linuxbee::ui {

// E-DIALOG, Save.
bool TagEditorDialog::save()
{
    // Each typed edit is checked in field order; the first problem stops the save.
    const QMap<QString, QString> typed = edits();
    for (const QString &field : tagEditorFields()) {
        if (!typed.contains(field))
            continue;
        const QString problem = editProblem(field, typed.value(field));
        if (problem.isEmpty())
            continue;
        d->problem->setText(problem);
        d->editors.value(field)->setFocus();
        return false;
    }

    // Every track with something to change goes to the saver, in order. A failure
    // does not stop the others.
    const QList<TagChanges> changes = pendingChanges();
    QList<qint64> written;
    QStringList failedNames;
    int failures = 0;
    for (int i = 0; i < d->targets.size(); ++i) {
        if (changes.at(i).isEmpty())
            continue;
        const EditTarget &target = d->targets.at(i);
        const WriteResult result = d->saver(target.path, changes.at(i));
        if (result.ok()) {
            // The file now says what the dialog's changes say, so the dialog's
            // idea of this track follows: an empty list removes the field.
            for (auto it = changes.at(i).fields.constBegin(); it != changes.at(i).fields.constEnd();
                ++it) {
                if (it.value().isEmpty())
                    d->targets[i].tags.fields.remove(it.key());
                else
                    d->targets[i].tags.fields.insert(it.key(), it.value());
            }
            if (result.status == WriteResult::Status::Ok)
                written.append(target.id);
        } else {
            ++failures;
            QString name = QFileInfo(target.path).fileName();
            // WS-LINK, WS-SAME: a file that was refused says why, in round brackets.
            if ((result.status == WriteResult::Status::HardLinked
                    || result.status == WriteResult::Status::ChangedOnDisk)
                && !result.problems.isEmpty())
                name += QStringLiteral(" (") + result.problems.first() + QLatin1Char(')');
            failedNames.append(name);
        }
    }

    if (!written.isEmpty())
        emit saved(written);

    if (failures == 0) {
        d->problem->clear();
        accept();
        return true;
    }
    d->problem->setText(tr("Could not save %1 of %2: %3")
            .arg(failures)
            .arg(d->targets.size())
            .arg(failedNames.join(QStringLiteral(", "))));
    return false;
}

}
