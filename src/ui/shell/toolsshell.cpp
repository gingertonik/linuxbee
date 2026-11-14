#include "shell/toolsshell.h"

#include "actions/actionregistry.h"
#include "mainwindow/mainwindow.h"
#include "shell/libraryshell.h"

#include <QCoreApplication>
#include <QDialog>
#include <QStatusBar>

namespace linuxbee::ui {

// The state and the four commands. The helpers live here, not in the header, which
// is fixed by the seed. Each command gets its dialog in its own task.
struct ToolsShell::Private {
    MainWindow *window = nullptr;
    ActionRegistry *actions = nullptr;
    Library *library = nullptr;
    LibraryShell *libraryShell = nullptr;
    QSettings *settings = nullptr;
    Presenter presenter;
    QList<PageFactory> pageFactories;
    QString message;

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

    void editTags()
    {
        if (selectedIds().isEmpty())
            return;
        // TS-TAGS: the tag editor is opened here in task 3.
    }

    void organise()
    {
        if (selectedIds().isEmpty())
            return;
        // TS-ORGANISE: the organiser is opened here in task 4.
    }

    void convert()
    {
        if (selectedIds().isEmpty())
            return;
        // TS-CONVERT: the converter is opened here in task 5.
    }

    void preferences()
    {
        // TS-PREFERENCES: needs no selection; the dialog is opened here in task 6.
    }
};

ToolsShell::ToolsShell(MainWindow *window, ActionRegistry *actions, Library *library,
    LibraryShell *libraryShell, QSettings *settings, Presenter presenter, QObject *parent)
    : QObject(parent)
    , d(new Private)
{
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
}

ToolsShell::~ToolsShell()
{
    delete d;
}

void ToolsShell::addPreferencesPage(PageFactory factory)
{
    Q_UNUSED(factory);
}

QStringList ToolsShell::preferencesPages() const
{
    return { };
}

QString ToolsShell::lastMessage() const
{
    return d->message;
}

}
