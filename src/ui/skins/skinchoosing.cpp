#include "skins/skinchoosing.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QStandardPaths>
#include <QStatusBar>

#include <algorithm>
#include <utility>

#include "actions/actionregistry.h"
#include "mainwindow/mainwindow.h"
#include "skin/skin.h"
#include "skins/skinapplier.h"

// Contract: docs/spec/skins.md (SC-x).
namespace linuxbee::ui {

namespace {

// Whether a file's name has this ending, in any case.
bool endsWithCI(const QString &name, const QString &ending)
{
    return name.endsWith(ending, Qt::CaseInsensitive);
}

// SC-LIST: a name a skin can have.
bool isSkinName(const QString &name)
{
    return endsWithCI(name, QStringLiteral(".xml")) && !name.startsWith(QLatin1Char('.'));
}

// SC-CHOOSE: the file's name without its folder and its ending.
QString skinNameOf(const QString &path)
{
    return QFileInfo(path).completeBaseName();
}

// One skin on offer, with what its place in the order is made of.
struct Found {
    QString path;
    QString key; // the name without its ending, case folded
    int depth = 0; // 0: directly in the skins folder
};

// SC-WIRING: the name a skin has in the default choice, the folder's name before it for a skin
// one folder down.
QString displayName(const QString &folder, const QString &path)
{
    const QFileInfo info(path);
    if (info.absolutePath() == QDir(folder).absolutePath())
        return info.completeBaseName();
    return QFileInfo(info.absolutePath()).fileName() + QLatin1Char('/') + info.completeBaseName();
}

// The hooks that are used when the caller gives none: the window's own dialogs.
SkinChoosing::Hooks defaultHooks(MainWindow *window, const QString &folder)
{
    SkinChoosing::Hooks hooks;
    hooks.chooseSkin = [window, folder](const QStringList &files,
                           const QString &current) -> std::optional<QString> {
        QStringList items;
        int start = 0;
        for (qsizetype i = 0; i < files.size(); ++i) {
            items.append(displayName(folder, files.at(i)));
            if (files.at(i) == current)
                start = static_cast<int>(i);
        }
        bool ok = false;
        const QString picked = QInputDialog::getItem(window, SkinChoosing::tr("Choose Skin"),
            SkinChoosing::tr("Skin:"), items, start, false, &ok);
        if (!ok)
            return std::nullopt;
        const qsizetype index = items.indexOf(picked);
        if (index < 0)
            return std::nullopt;
        return files.at(index);
    };
    hooks.chooseFile = [window]() -> std::optional<QString> {
        const QString file = QFileDialog::getOpenFileName(window, SkinChoosing::tr("Add Skin"),
            QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
            SkinChoosing::tr("Skin files (*.xml *.xmlc);;All files (*)"));
        if (file.isEmpty())
            return std::nullopt;
        return file;
    };
    return hooks;
}

}

struct SkinChoosing::Private {
    MainWindow *window = nullptr;
    SkinApplier *applier = nullptr;
    QString folder;
    Hooks hooks;
    QString message; // SC-MESSAGE: the last outcome that was reported

    // SC-MESSAGE: shows the text in the status bar and keeps it.
    void report(const QString &text)
    {
        message = text;
        window->statusBar()->showMessage(text);
    }

    // SC-LIST: lists names only; no file is opened.
    QStringList list() const
    {
        const QDir top(folder);
        QList<Found> found;
        const QFileInfoList entries
            = top.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &entry : entries) {
            if (entry.fileName().startsWith(QLatin1Char('.')))
                continue;
            if (entry.isDir()) {
                // A folder named like a skin is a folder, not a skin; its files are offered.
                const QFileInfoList inner
                    = QDir(entry.absoluteFilePath()).entryInfoList(QDir::Files);
                for (const QFileInfo &file : inner) {
                    if (isSkinName(file.fileName()))
                        found.append(
                            { file.absoluteFilePath(), file.completeBaseName().toCaseFolded(), 1 });
                }
            } else if (isSkinName(entry.fileName())) {
                found.append(
                    { entry.absoluteFilePath(), entry.completeBaseName().toCaseFolded(), 0 });
            }
        }
        std::sort(found.begin(), found.end(), [](const Found &a, const Found &b) {
            if (a.key != b.key)
                return a.key < b.key;
            if (a.depth != b.depth)
                return a.depth < b.depth;
            return a.path < b.path;
        });
        QStringList paths;
        for (const Found &one : found)
            paths.append(one.path);
        return paths;
    }

    // SC-CHOOSE: gives the skin to the applier once and says what came of it.
    bool use(const QString &file)
    {
        const QString name = skinNameOf(file);
        if (!applier->apply(file)) {
            report(QStringLiteral("Could not use the skin ") + name + QStringLiteral(": ")
                + applier->lastError());
            return false;
        }
        report(QStringLiteral("Skin: ") + name);
        return true;
    }

    // SC-ADD: whether the skins folder, or a folder directly in it, has anything of this name
    // (case ignored): a file, a folder, a link, hidden or not.
    bool hasName(const QString &name) const
    {
        const QDir::Filters all
            = QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot;
        const QStringList topNames = QDir(folder).entryList(all);
        for (const QString &entry : topNames) {
            if (entry.compare(name, Qt::CaseInsensitive) == 0)
                return true;
        }
        const QStringList folders = QDir(folder).entryList(
            QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot | QDir::NoSymLinks);
        for (const QString &sub : folders) {
            const QStringList names = QDir(QDir(folder).absoluteFilePath(sub)).entryList(all);
            for (const QString &entry : names) {
                if (entry.compare(name, Qt::CaseInsensitive) == 0)
                    return true;
            }
        }
        return false;
    }

    // SC-ADD: the reason a file is not to be added, or empty. `bytes` gets the file's content.
    QString refusal(const QString &file, QByteArray *bytes) const
    {
        const QString name = QFileInfo(file).fileName();
        if (endsWithCI(name, QStringLiteral(".xmlc")))
            return SkinChoosing::tr("linuxBee cannot read compiled skins (.xmlc)");
        if (!endsWithCI(name, QStringLiteral(".xml")))
            return SkinChoosing::tr("a skin file ends in .xml");
        const QFileInfo info(file);
        if (info.exists() && !info.isFile())
            return SkinChoosing::tr("it is not a file");
        QFile in(file);
        if (!in.open(QIODevice::ReadOnly))
            return in.errorString();
        *bytes = in.readAll();
        const Skin skin = readSkin(*bytes);
        if (!skin.ok)
            return skin.error;
        if (hasName(name))
            return SkinChoosing::tr("there is a skin of that name already");
        return QString();
    }

    // SC-ADD, the whole of it.
    bool add()
    {
        const std::optional<QString> answer = hooks.chooseFile();
        if (!answer)
            return false;
        const QString source = QFileInfo(*answer).absoluteFilePath();
        const QString name = QFileInfo(source).fileName();
        const QString shown = skinNameOf(source);
        QByteArray bytes;
        const QString reason = refusal(source, &bytes);
        if (!reason.isEmpty()) {
            report(
                QStringLiteral("Could not add the skin ") + shown + QStringLiteral(": ") + reason);
            return false;
        }

        // What it is laid over, when that is a name beside the original and not in the skins
        // folder yet. It is copied whether linuxBee can read it or not.
        QStringList names { name };
        const QString base = readSkin(bytes).dependsOn;
        if (!base.isEmpty() && QFileInfo(base).fileName() == base
            && base.compare(name, Qt::CaseInsensitive) != 0
            && QFileInfo(QFileInfo(source).dir().filePath(base)).isFile() && !hasName(base))
            names.append(base);

        // The folders this makes are noted, so a failure leaves none of them behind.
        QStringList made;
        for (QString dir = QDir(folder).absolutePath(); !QFileInfo::exists(dir);
            dir = QFileInfo(dir).absolutePath())
            made.append(dir);
        QStringList copied;
        bool failed = !QDir().mkpath(folder);
        for (qsizetype i = 0; !failed && i < names.size(); ++i) {
            const QString to = QDir(folder).absoluteFilePath(names.at(i));
            // QFile::copy never writes over a file that is there.
            if (QFile::copy(QFileInfo(source).dir().filePath(names.at(i)), to))
                copied.append(to);
            else
                failed = true;
        }
        if (failed) {
            for (const QString &file : std::as_const(copied))
                QFile::remove(file); // only what was copied just now
            for (const QString &dir : std::as_const(made))
                QDir().rmdir(dir); // empty folders this made, deepest first
            report(QStringLiteral("Could not add the skin ") + shown
                + QStringLiteral(": the file could not be copied"));
            return false;
        }
        if (!applier->apply(copied.first())) {
            report(QStringLiteral("Could not use the skin ") + shown + QStringLiteral(": ")
                + applier->lastError());
            return false;
        }
        report(QStringLiteral("Added the skin ") + shown);
        return true;
    }
};

SkinChoosing::SkinChoosing(MainWindow *window, ActionRegistry *actions, SkinApplier *applier,
    const QString &folder, Hooks hooks, QObject *parent)
    : QObject(parent)
    , d(new Private)
{
    d->window = window;
    d->applier = applier;
    d->folder = folder;
    d->hooks = std::move(hooks);
    const Hooks fallback = defaultHooks(window, folder);
    if (!d->hooks.chooseSkin)
        d->hooks.chooseSkin = fallback.chooseSkin;
    if (!d->hooks.chooseFile)
        d->hooks.chooseFile = fallback.chooseFile;

    // SC-WIRING: View > Skins. Destroying this object frees the commands.
    actions->implement(QStringLiteral("view.skins.choose"), this, [this] { choose(); });
    actions->implement(QStringLiteral("view.skins.add"), this, [this] { add(); });
    actions->implement(QStringLiteral("view.skins.none"), this, [this] { none(); });
}

SkinChoosing::~SkinChoosing()
{
    delete d;
}

QString SkinChoosing::folder() const
{
    return d->folder;
}

QStringList SkinChoosing::skinFiles() const
{
    return d->list();
}

bool SkinChoosing::choose()
{
    const QStringList files = d->list();
    if (files.isEmpty()) {
        d->report(QStringLiteral("There are no skins yet. Add one with View > Skins > Add Skin")
            + QChar(0x2026));
        return false;
    }
    const std::optional<QString> answer = d->hooks.chooseSkin(files, d->applier->currentFile());
    if (!answer)
        return false;
    return d->use(*answer);
}

bool SkinChoosing::add()
{
    return d->add();
}

bool SkinChoosing::none()
{
    if (d->applier->currentFile().isEmpty())
        return false;
    d->applier->clear();
    d->report(QStringLiteral("Skin: none"));
    return true;
}

QString SkinChoosing::lastMessage() const
{
    return d->message;
}

}
