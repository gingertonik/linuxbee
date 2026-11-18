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
#include "tagundo/tagundo.h"

#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QStandardPaths>
#include <QSet>
#include <QStatusBar>

#include <cstdlib>
#include <memory>

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

    // TS-UNDO, the run folder: this shell's folder under the cache folder, `run-` and six
    // characters, with the lock it holds while it lives. When the shell goes, the lock is
    // let go and the folder removed with what is in it. Nothing else is ever removed here.
    struct RunFolder {
        QString root;
        QString path; // empty when no folder could be made
        std::unique_ptr<QLockFile> lock;

        // TS-UNDO: the folders of dead runs go; a held one, and any other name, stay.
        // A link is removed itself and never followed (isSymLink before isDir).
        static void tidy(const QString &root)
        {
            const QDir dir(root);
            const QFileInfoList entries = dir.entryInfoList({ QStringLiteral("run-*") },
                QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
            for (const QFileInfo &entry : entries) {
                if (!entry.fileName().startsWith(QLatin1String("run-")))
                    continue;
                if (entry.isSymLink() || !entry.isDir()) {
                    QFile::remove(entry.absoluteFilePath());
                    continue;
                }
                // Judged by the holder being alive, never by the age of the file.
                QLockFile held(entry.absoluteFilePath() + QStringLiteral("/lock"));
                held.setStaleLockTime(0);
                if (!held.tryLock(0))
                    continue;
                held.unlock();
                QDir(entry.absoluteFilePath()).removeRecursively();
            }
        }

        RunFolder()
            : root(QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
                  + QStringLiteral("/linuxbee/undo"))
        {
            // The last name of the root must be a folder, not a link; judged before and
            // after it is made. Folders above it may be links.
            if (QFileInfo(root).isSymLink() || !QDir().mkpath(root) || QFileInfo(root).isSymLink())
                return;
            // One start at a time: tidying and making the folder happen under this lock,
            // let go on every way out (it goes out of scope with the constructor).
            QLockFile start(root + QStringLiteral("/start.lock"));
            start.setStaleLockTime(0);
            if (!start.tryLock(2000))
                return;
            tidy(root);
            // mkdtemp makes the folder with mode 0700 at once.
            QByteArray name = QFile::encodeName(root + QStringLiteral("/run-XXXXXX"));
            if (!mkdtemp(name.data()))
                return;
            const QString made = QFile::decodeName(name);
            auto held = std::make_unique<QLockFile>(made + QStringLiteral("/lock"));
            held->setStaleLockTime(0);
            if (!held->tryLock(0)) {
                QDir(made).removeRecursively();
                return;
            }
            lock = std::move(held);
            path = made;
        }

        ~RunFolder()
        {
            if (path.isEmpty())
                return;
            lock->unlock();
            lock.reset();
            QDir(path).removeRecursively();
        }

        // Where the undo keeps pictures. With no folder, an empty text: the undo then keeps
        // no picture at all, and no stand-in path is used.
        QString pictures() const
        {
            return path.isEmpty() ? QString() : path + QStringLiteral("/pictures");
        }
    };

    // The folder is declared before the undo: members go in reverse order, so the undo
    // removes its files first, then the lock is let go, then the folder is removed.
    RunFolder house;
    std::unique_ptr<TagUndo> undo;
    bool changeOpen = false; // between begin and end of a dialog the shell presented

    // TS-UNDO: begin(the dialog's title) before the presenter, end() after it, however
    // the presenter is left.
    struct Step {
        Private *p;
        Step(Private *owner, const QString &name)
            : p(owner)
        {
            p->changeOpen = true;
            p->notKeptNames.clear(); // a dialog closed without a message tells nothing later
            p->notKeptFiles = 0;
            p->undo->begin(name);
        }
        ~Step()
        {
            p->undo->end();
            p->changeOpen = false;
        }
        Step(const Step &) = delete;
        Step &operator=(const Step &) = delete;
    };

    // TS-TAGS, not kept: what the saves since the last message could not keep in ID3v2.3,
    // the names once each and the number of files that had any. Told once, then forgotten.
    QSet<QString> notKeptNames;
    int notKeptFiles = 0;

    TagUndo::Writer saver()
    {
        return [this](const QString &path, const TagChanges &changes) {
            const WriteResult result = undo->write(path, changes);
            if (result.status == WriteResult::Status::Ok && !result.notKept.isEmpty()) {
                ++notKeptFiles;
                for (const QString &line : result.notKept) {
                    const qsizetype colon = line.indexOf(QStringLiteral(": "));
                    notKeptNames.insert(colon < 0 ? line : line.left(colon));
                }
            }
            return result;
        };
    }

    // The text that goes on a save message, empty when nothing was lost; forgets it.
    QString takeNotKept()
    {
        QString text;
        if (notKeptFiles > 0) {
            QStringList names(notKeptNames.cbegin(), notKeptNames.cend());
            names.sort();
            text = QCoreApplication::translate(
                "linuxbee::ui::ToolsShell", "; not kept in ID3v2.3: %1 (%2)")
                       .arg(names.join(QStringLiteral(", ")),
                           notKeptFiles == 1
                               ? QCoreApplication::translate("linuxbee::ui::ToolsShell", "1 file")
                               : QCoreApplication::translate("linuxbee::ui::ToolsShell", "%1 files")
                                     .arg(notKeptFiles));
        }
        notKeptNames.clear();
        notKeptFiles = 0;
        return text;
    }

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
            // TS-TOOLS, Pictures: the library keeps only a count, so each picture
            // becomes an entry without data, as a read without picture data gives.
            for (int i = 0; i < track->pictureCount; ++i)
                target.tags.pictures.append(Picture());
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
        dialog.setSaver(saver());
        QObject::connect(&dialog, &TagEditorDialog::saved, owner,
            [this](const QList<qint64> &saved) { tagsSaved(saved); });
        const Step step(this, dialog.windowTitle());
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
        showMessage(text + takeNotKept());
    }

    // TS-TOOLS: the tagging-tool dialog with the panel, on the selected tracks, with the
    // shell's settings so that the options are remembered. The dialog takes the panel
    // as its child.
    void tagTool(ToolPanel *panel, const QList<qint64> &ids)
    {
        TagToolDialog dialog(panel, targetsFor(ids), settings, window);
        dialog.setSaver(saver());
        QObject::connect(&dialog, &TagToolDialog::saved, owner,
            [this](const QList<qint64> &saved) { tagsSaved(saved); });
        const Step step(this, dialog.windowTitle());
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

    // TS-UNDO: asks nothing. With no step, says so. Otherwise puts the step back, has the
    // library read each restored file it knows once, and says how many went and how many not.
    void undoLast()
    {
        if (changeOpen) {
            showMessage(QCoreApplication::translate(
                "linuxbee::ui::ToolsShell", "Undo is not available while a change is being made"));
            return;
        }
        if (!undo->canUndo()) {
            showMessage(QCoreApplication::translate("linuxbee::ui::ToolsShell", "Nothing to undo"));
            return;
        }
        const QString name = undo->name();
        const TagUndo::Outcome outcome = undo->undo();
        for (const QString &path : outcome.restored) {
            if (const std::optional<LibraryTrack> track = library->trackByPath(path))
                library->refresh(track->id);
        }
        const qsizetype put = outcome.restored.size();
        QString text = put == 1
            ? QCoreApplication::translate("linuxbee::ui::ToolsShell", "Undid %1: 1 file put back")
                  .arg(name)
            : QCoreApplication::translate("linuxbee::ui::ToolsShell", "Undid %1: %2 files put back")
                  .arg(name)
                  .arg(put);
        // UN-SAME: files that are no longer the file that was edited were left as they are.
        if (outcome.changed.size() == 1)
            text += QCoreApplication::translate(
                "linuxbee::ui::ToolsShell", "; 1 changed or moved since, and left as it is");
        else if (!outcome.changed.isEmpty())
            text += QCoreApplication::translate(
                "linuxbee::ui::ToolsShell", "; %1 changed or moved since, and left as they are")
                        .arg(outcome.changed.size());
        if (!outcome.failed.isEmpty())
            text += QCoreApplication::translate(
                "linuxbee::ui::ToolsShell", "; %1 could not be put back")
                        .arg(outcome.failed.size());
        showMessage(text);
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

    // TS-ORGANISE: a file that was moved keeps its library track, now at the new place. The
    // moved outcomes go, in the report's order, to one recordMoves, which does to the tracks
    // what was done to the files; the playlists and the queue are brought up to date once for
    // the run (TS-ORGANISE, L-MOVED). The shell looks at nothing else.
    void organisedFiles(const OrganiseReport &report)
    {
        QList<FileMove> moves;
        for (const MoveOutcome &outcome : report.outcomes) {
            if (outcome.status == MoveOutcome::Status::Done && outcome.moved
                && outcome.target != outcome.source)
                moves.append({ outcome.source, outcome.target });
        }
        // TS-ORGANISE: the library saying `failed` during this one call, and only then, ends the
        // line with what the user needs to know.
        bool refused = false;
        if (!moves.isEmpty()) {
            const QMetaObject::Connection listening = QObject::connect(
                library, &Library::failed, owner, [&refused] { refused = true; });
            library->recordMoves(moves);
            QObject::disconnect(listening);
        }
        QString line = QCoreApplication::translate(
            "linuxbee::ui::ToolsShell", "Organised: %1 done, %2 skipped, %3 failed")
                           .arg(report.done)
                           .arg(report.skipped)
                           .arg(report.failed);
        if (refused)
            line += QCoreApplication::translate("linuxbee::ui::ToolsShell",
                "; the library could not be changed and still lists the files where they were");
        showMessage(line);
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
    d->undo = std::make_unique<TagUndo>(d->house.pictures());
    // TS-UNDO, UN-MOVED: the undo is told of every move the library reports, so a change is
    // undone in the file where the move has put it. The undo dies with the shell, so the
    // connection ends with it.
    TagUndo *const undo = d->undo.get();
    connect(library, &Library::pathsMoved, undo,
        [undo](const QHash<QString, QString> &oldToNew) { undo->moved(oldToNew); });
    d->presenter
        = presenter ? std::move(presenter) : Presenter([](QDialog *dialog) { dialog->exec(); });

    // TS-COMMANDS: the shell is the context of its commands, so destroying the shell
    // frees them and the ids are free again.
    d->actions->implement(QStringLiteral("file.editTags"), this, [this] { d->editTags(); });
    d->actions->implement(QStringLiteral("edit.undo"), this, [this] { d->undoLast(); });
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

TagUndo *ToolsShell::tagUndo() const
{
    return d->undo.get();
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
