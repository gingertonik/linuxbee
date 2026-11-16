#include "shell/toolsshell.h"

#include "actions/actionregistry.h"
#include "mainwindow/mainwindow.h"
#include "shell/libraryshell.h"
#include "convert/convertdialog.h"
#include "organise/organisedialog.h"
#include "preferences/hotkeyspage.h"
#include "preferences/preferencesdialog.h"
#include "tageditor/tageditordialog.h"
#include "tagtools/capitalisepanel.h"
#include "tagtools/inferpanel.h"
#include "tagtools/removetagspanel.h"
#include "tagtools/renumberpanel.h"
#include "tagtools/searchreplacepanel.h"
#include "tagtools/tagtooldialog.h"

#include <QCoreApplication>
#include <QDialog>
#include <QFile>
#include <QStatusBar>

namespace linuxbee::ui {

// The state and the six commands. The helpers live here, not in the header, which
// is fixed by the seed.
struct ToolsShell::Private {
    MainWindow *window = nullptr;
    ActionRegistry *actions = nullptr;
    Library *library = nullptr;
    LibraryShell *libraryShell = nullptr;
    QSettings *settings = nullptr;
    Presenter presenter;
    QList<PageFactory> pageFactories;
    QString message;
    ToolsShell *owner = nullptr; // the context of the dialogs' connections

    // TS-MESSAGE: one line in the window's status bar, kept as lastMessage().
    void showMessage(const QString &text)
    {
        message = text;
        window->statusBar()->showMessage(text);
    }

    // TS-SELECTION: the selected tracks in the order shown. With none, the message
    // says so and the list is empty.
    QList<qint64> selectedIds()
    {
        QList<qint64> ids = libraryShell->selectedTrackIds();
        if (ids.isEmpty())
            showMessage(QCoreApplication::translate(
                "linuxbee::ui::ToolsShell", "Select one or more tracks first"));
        return ids;
    }

    // TS-TAGS: one target per selected track, with the tags the library holds for it.
    // The tagging tools are given the same targets (TS-TOOLS).
    QList<EditTarget> targetsFor(const QList<qint64> &ids)
    {
        QList<EditTarget> targets;
        for (const qint64 id : ids) {
            const std::optional<LibraryTrack> track = library->track(id);
            if (!track)
                continue;
            EditTarget target;
            target.id = track->id;
            target.path = track->path;
            target.tags = track->tags;
            targets.append(target);
        }
        return targets;
    }

    void editTags()
    {
        const QList<qint64> ids = selectedIds();
        if (ids.isEmpty())
            return;
        TagEditorDialog dialog(targetsFor(ids), window);
        QObject::connect(&dialog, &TagEditorDialog::saved, owner,
            [this](const QList<qint64> &saved) { tagsSaved(saved); });
        presenter(&dialog);
    }

    // TS-TAGS: the files written are read again by the library, then the window is told.
    void tagsSaved(const QList<qint64> &saved)
    {
        if (saved.isEmpty())
            return;
        for (const qint64 id : saved)
            library->refresh(id);
        const QString text = saved.size() == 1
            ? QCoreApplication::translate("linuxbee::ui::ToolsShell", "Saved tags of 1 track")
            : QCoreApplication::translate("linuxbee::ui::ToolsShell", "Saved tags of %1 tracks")
                  .arg(saved.size());
        showMessage(text);
    }

    // TS-TOOLS: the tagging-tool dialog with the panel, on the selected tracks, with the
    // shell's settings so that the options are remembered. The dialog takes the panel
    // as its child.
    void tagTool(ToolPanel *panel, const QList<qint64> &ids)
    {
        TagToolDialog dialog(panel, targetsFor(ids), settings, window);
        QObject::connect(&dialog, &TagToolDialog::saved, owner,
            [this](const QList<qint64> &saved) { tagsSaved(saved); });
        presenter(&dialog);
    }

    // TS-TOOLS, TS-SELECTION: with nothing selected the message is given and no panel is made.
    void searchReplace()
    {
        const QList<qint64> ids = selectedIds();
        if (!ids.isEmpty())
            tagTool(new SearchReplacePanel(), ids);
    }

    void capitalise()
    {
        const QList<qint64> ids = selectedIds();
        if (!ids.isEmpty())
            tagTool(new CapitalisePanel(), ids);
    }

    void removeTags()
    {
        const QList<qint64> ids = selectedIds();
        if (!ids.isEmpty())
            tagTool(new RemoveTagsPanel(), ids);
    }

    void renumber()
    {
        const QList<qint64> ids = selectedIds();
        if (!ids.isEmpty())
            tagTool(new RenumberPanel(), ids);
    }

    void inferFromFilename()
    {
        const QList<qint64> ids = selectedIds();
        if (!ids.isEmpty())
            tagTool(new InferPanel(), ids);
    }

    // TS-ORGANISE: the organiser is given the selected paths and the settings.
    void organise()
    {
        if (selectedIds().isEmpty())
            return;
        OrganiseDialog dialog(libraryShell->selectedPaths(), settings, window);
        QObject::connect(&dialog, &OrganiseDialog::organised, owner,
            [this](const OrganiseReport &report) { organisedFiles(report); });
        presenter(&dialog);
    }

    // TS-ORGANISE: a file that was moved keeps its library track, now at the new place.
    // A copy leaves the library as it was: its source still exists. A track that held the
    // target goes first, since its file is no longer there, so the path is free to take.
    void organisedFiles(const OrganiseReport &report)
    {
        for (const MoveOutcome &outcome : report.outcomes) {
            if (outcome.status != MoveOutcome::Status::Done || outcome.target == outcome.source
                || QFile::exists(outcome.source))
                continue;
            if (const std::optional<LibraryTrack> held = library->trackByPath(outcome.target))
                library->remove({ held->id });
            if (const std::optional<LibraryTrack> track = library->trackByPath(outcome.source))
                library->setPath(track->id, outcome.target);
        }
        showMessage(QCoreApplication::translate(
            "linuxbee::ui::ToolsShell", "Organised: %1 done, %2 skipped, %3 failed")
                .arg(report.done)
                .arg(report.skipped)
                .arg(report.failed));
    }

    // TS-CONVERT: the converter is given the selected paths and the settings.
    void convert()
    {
        if (selectedIds().isEmpty())
            return;
        ConvertDialog dialog(libraryShell->selectedPaths(), settings, window);
        QObject::connect(&dialog, &ConvertDialog::converted, owner,
            [this](const QStringList &paths) { convertedFiles(paths); });
        presenter(&dialog);
    }

    // TS-CONVERT: the results are not added to the library. A run that reports no
    // results gives no message; the spec is silent on that case.
    void convertedFiles(const QStringList &paths)
    {
        if (paths.isEmpty())
            return;
        const QString text = paths.size() == 1
            ? QCoreApplication::translate("linuxbee::ui::ToolsShell", "Converted 1 file")
            : QCoreApplication::translate("linuxbee::ui::ToolsShell", "Converted %1 files")
                  .arg(paths.size());
        showMessage(text);
    }

    // TS-PREFERENCES: needs no selection. The dialog is built as the command builds it
    // and handed to the presenter.
    void preferences()
    {
        PreferencesDialog dialog(window);
        fillPreferences(&dialog);
        presenter(&dialog);
    }

    // TS-PREFERENCES: the Hotkeys page first, then the page of each factory in the order
    // added. A factory that gives no page adds nothing. The dialog owns each page.
    void fillPreferences(PreferencesDialog *dialog)
    {
        dialog->addPage(new HotkeysPage(actions, settings));
        for (const PageFactory &factory : pageFactories) {
            if (PreferencesPage *page = factory())
                dialog->addPage(page);
        }
    }
};

ToolsShell::ToolsShell(MainWindow *window, ActionRegistry *actions, Library *library,
    LibraryShell *libraryShell, QSettings *settings, Presenter presenter, QObject *parent)
    : QObject(parent)
    , d(new Private)
{
    d->owner = this;
    d->window = window;
    d->actions = actions;
    d->library = library;
    d->libraryShell = libraryShell;
    d->settings = settings;
    d->presenter
        = presenter ? std::move(presenter) : Presenter([](QDialog *dialog) { dialog->exec(); });

    // TS-COMMANDS: the shell is the context of its commands, so destroying the shell
    // frees them and the ids are free again.
    d->actions->implement(QStringLiteral("file.editTags"), this, [this] { d->editTags(); });
    d->actions->implement(QStringLiteral("file.organise"), this, [this] { d->organise(); });
    d->actions->implement(QStringLiteral("tools.convert"), this, [this] { d->convert(); });
    d->actions->implement(QStringLiteral("edit.preferences"), this, [this] { d->preferences(); });
    d->actions->implement(
        QStringLiteral("tools.tagging.searchReplace"), this, [this] { d->searchReplace(); });
    d->actions->implement(
        QStringLiteral("tools.tagging.capitalise"), this, [this] { d->capitalise(); });
    d->actions->implement(
        QStringLiteral("tools.tagging.removeTags"), this, [this] { d->removeTags(); });
    d->actions->implement(
        QStringLiteral("tools.tagging.renumber"), this, [this] { d->renumber(); });
    d->actions->implement(QStringLiteral("tools.tagging.inferFromFilename"), this,
        [this] { d->inferFromFilename(); });
}

ToolsShell::~ToolsShell()
{
    delete d;
}

void ToolsShell::addPreferencesPage(PageFactory factory)
{
    // TS-PREFERENCES: an empty factory adds nothing.
    if (factory)
        d->pageFactories.append(std::move(factory));
}

QStringList ToolsShell::preferencesPages() const
{
    // The titles only: the pages are built for this dialog, which is never shown, and
    // they die with it.
    PreferencesDialog dialog;
    d->fillPreferences(&dialog);
    return dialog.pages();
}

QString ToolsShell::lastMessage() const
{
    return d->message;
}

}
