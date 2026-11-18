#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTextStream>

#include <sys/stat.h>

#include "command.h"
#include "settingsimport/settingsimport.h"

namespace {

using linuxbee::cli::usageError;

const char *const kSettingsUsage = "usage: linuxbeectl settings import FILE\n"
                                   "       linuxbeectl settings import --apply --to INI FILE";

// SI-CLI: a file that cannot be read, or is not a settings file, or a settings
// file that cannot be written, is reported as "failed: FILE: REASON" on standard
// error, with nothing on standard output, and exits 3.
int failed(const QString &path, const QString &reason)
{
    QTextStream err(stderr);
    err << QStringLiteral("failed: %1: %2").arg(path, reason) << '\n';
    err.flush();
    return 3;
}

// SI-CLI-TARGET: a --to that is there and is not a settings file is never written
// over. Returns why it is refused, or an empty string when it may be written. Only
// reads: nothing is written here and no folder is made.
QString targetRefusal(const QString &ini)
{
    const QFileInfo info(ini);
    if (!info.exists())
        return QString();
    if (info.isDir())
        return QStringLiteral("it is a folder");
    QSettings probe(ini, QSettings::IniFormat);
    probe.allKeys();
    if (probe.status() != QSettings::NoError)
        return QStringLiteral("it cannot be read as an INI settings file");
    return QString();
}

// SI-CLI-SELF: the two paths are one file when both can be asked and have the same device and
// inode (stat follows links, so a link or a second name is the file), and otherwise when
// their cleaned absolute paths are the same.
bool sameFile(const QString &a, const QString &b)
{
    struct stat first;
    struct stat second;
    if (::stat(QFile::encodeName(a).constData(), &first) == 0
        && ::stat(QFile::encodeName(b).constData(), &second) == 0)
        return first.st_dev == second.st_dev && first.st_ino == second.st_ino;
    return QDir::cleanPath(QFileInfo(a).absoluteFilePath())
        == QDir::cleanPath(QFileInfo(b).absoluteFilePath());
}

// The path of the rule "the path written is the path checked": the one path a command looks at and
// writes, made once from the path as the user wrote it. First as text (absolute, `.` and `..` taken
// out without asking the disk), then by the system: what is there is given by its real path, every
// link followed; where nothing is, the real path of its folder and the name (a folder that is not
// there either leaves the text path). A link that leads nowhere is not followed, and is said so.
struct WrittenPath {
    QString path;
    bool linkToNothing = false;
};

WrittenPath writtenPath(const QString &given)
{
    WrittenPath result;
    result.path = QDir::cleanPath(QFileInfo(given).absoluteFilePath());
    const QByteArray name = QFile::encodeName(result.path);
    struct stat info;
    if (::lstat(name.constData(), &info) == 0) {
        if (::stat(name.constData(), &info) != 0) {
            result.linkToNothing = true;
            return result;
        }
        const QString real = QFileInfo(result.path).canonicalFilePath();
        if (!real.isEmpty())
            result.path = real;
        return result;
    }
    const QFileInfo text(result.path);
    const QString folder = QFileInfo(text.absolutePath()).canonicalFilePath();
    if (!folder.isEmpty())
        result.path = QDir(folder).filePath(text.fileName());
    return result;
}

// SI-CLI: linuxbeectl settings import. Prints what would be carried over, line by
// line; with --apply, writes the imported lines to the INI file given with --to.
int runSettings(const QStringList &arguments)
{
    QCommandLineParser parser;
    QCommandLineOption applyOption(QStringLiteral("apply"),
        QStringLiteral("Write the imported settings to the file given with --to."));
    QCommandLineOption toOption(QStringLiteral("to"),
        QStringLiteral("The settings file to write, in INI format."), QStringLiteral("INI"));
    parser.addOptions({ applyOption, toOption });

    // parse(), not process(): process() exits 1 on an unknown option, and SI-CLI wants 2.
    if (!parser.parse(QStringList { QStringLiteral("linuxbeectl settings") } + arguments))
        return usageError(parser.errorText());

    const QStringList positional = parser.positionalArguments();
    if (positional.isEmpty())
        return usageError(QStringLiteral("linuxbeectl: settings needs import.\n%1")
                .arg(QLatin1String(kSettingsUsage)));
    if (positional.at(0) != QLatin1String("import"))
        return usageError(QStringLiteral("linuxbeectl: settings has no subcommand \"%1\".\n%2")
                .arg(positional.at(0), QLatin1String(kSettingsUsage)));
    if (positional.size() != 2)
        return usageError(QStringLiteral("linuxbeectl: settings import takes exactly one file.\n%1")
                .arg(QLatin1String(kSettingsUsage)));

    const bool apply = parser.isSet(applyOption);
    const bool toGiven = parser.isSet(toOption);
    if (apply && (!toGiven || parser.value(toOption).isEmpty()))
        return usageError(QStringLiteral("linuxbeectl: --apply needs --to INI.\n%1")
                .arg(QLatin1String(kSettingsUsage)));
    if (toGiven && !apply)
        return usageError(QStringLiteral("linuxbeectl: --to needs --apply.\n%1")
                .arg(QLatin1String(kSettingsUsage)));

    const QString path = positional.at(1);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return failed(path, file.errorString());
    const QByteArray bytes = file.readAll();
    file.close();

    const linuxbee::MusicBeeSettings settings = linuxbee::readMusicBeeSettings(bytes);
    if (!settings.ok)
        return failed(path, settings.error);
    const QList<linuxbee::SettingsImportLine> plan = linuxbee::planSettingsImport(settings);

    // Nothing is written unless --apply is given. The write happens before any line is
    // printed, so a write that fails leaves standard output empty, as a failed read does.
    int written = 0;
    if (apply) {
        // --to as it was given, for the messages; the path of the rule is the one that is
        // looked at, the one whose folder is made and the one that is written.
        const QString ini = parser.value(toOption);
        const WrittenPath resolved = writtenPath(ini);
        // SI-CLI-PATH: a link that leads to nothing is never written through.
        if (resolved.linkToNothing)
            return failed(ini, QStringLiteral("it is a link to nothing"));
        const QString to = resolved.path;
        // SI-CLI-SELF: the file being imported is never the one written, under
        // any spelling or through a link.
        if (sameFile(path, to))
            return failed(ini, QStringLiteral("this is the file being imported"));
        // SI-CLI-TARGET: refused before a folder is made or anything is written.
        const QString refusal = targetRefusal(to);
        if (!refusal.isEmpty())
            return failed(ini, refusal);
        QDir().mkpath(QFileInfo(to).absolutePath());
        QSettings target(to, QSettings::IniFormat);
        written = linuxbee::applySettingsImport(plan, target);
        target.sync();
        if (target.status() != QSettings::NoError)
            return failed(ini, QStringLiteral("could not write the settings"));
    }

    QTextStream out(stdout);
    qsizetype imported = 0;
    for (const linuxbee::SettingsImportLine &line : plan) {
        if (line.imported()) {
            out << "import " << line.key << " = " << line.value << " (" << line.what << ")\n";
            ++imported;
        } else {
            out << "skip " << line.what << ": " << line.note << '\n';
        }
    }
    const qsizetype skipped = plan.size() - imported;
    if (apply)
        out << written << " written, " << skipped << " skipped\n";
    else
        out << imported << " to import, " << skipped << " skipped\n";
    out.flush();
    return 0;
}

}

LINUXBEE_COMMAND(settings, "settings",
    "Carry over hotkeys and the naming template from a MusicBee settings file.", runSettings)
