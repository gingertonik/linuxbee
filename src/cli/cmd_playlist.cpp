#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>

#include <sys/stat.h>

#include <optional>

#include "command.h"
#include "playlists/playlistformat.h"

namespace {

using linuxbee::cli::usageError;

const char *const kUsage
    = "usage: linuxbeectl playlist show FILE\n"
      "       linuxbeectl playlist convert [--relative] [--plain] [--force] IN OUT\n"
      "FILE, IN and OUT are read and written by their extension: .m3u, .m3u8 or .pls.";

// Y-CLI: exit 3 with "failed: FILE: ..." on standard error.
int fileError(const QString &file, const QString &reason)
{
    QTextStream out(stderr);
    out << QStringLiteral("failed: %1: %2").arg(file, reason) << '\n';
    out.flush();
    return 3;
}

// Y-CLI: a file name that is not .m3u, .m3u8 or .pls is a usage mistake.
QString notAPlaylist(const QString &label, const QString &file)
{
    return QStringLiteral("linuxbeectl: %1 \"%2\" is not a playlist file (.m3u, .m3u8, .pls).\n%3")
        .arg(label, file, QLatin1String(kUsage));
}

// The folder a playlist file is in: Y-PATH resolves relative locations
// against it, Y-WRITE writes relative locations against it.
QString folderOf(const QString &file)
{
    return QFileInfo(file).absolutePath();
}

int showPlaylist(const QString &file)
{
    const auto format = linuxbee::playlistFormatForFile(QFileInfo(file).fileName());
    if (!format)
        return usageError(notAPlaylist(QStringLiteral("FILE"), file));
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly))
        return fileError(file, QStringLiteral("cannot be read"));
    const QByteArray data = in.readAll();
    in.close();

    // Y-CLI: one line per entry: location, tab, seconds, tab, title.
    QTextStream out(stdout);
    for (const linuxbee::PlaylistEntry &entry :
        linuxbee::parsePlaylist(data, *format, folderOf(file)))
        out << entry.path << '\t' << entry.durationSeconds << '\t' << entry.title << '\n';
    return 0;
}

// Y-CLI: OUT is the file IN is: the same device and inode when both can be asked
// (stat follows links, so a link to the file is the file), otherwise the same cleaned path.
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

// Y-CLI: anything at OUT counts, a link to nothing too (lstat does not follow it).
bool somethingIsAt(const QString &file)
{
    struct stat info;
    return ::lstat(QFile::encodeName(file).constData(), &info) == 0;
}

int convertPlaylist(const QStringList &positional, const QCommandLineParser &parser,
    const QCommandLineOption &relativeOption, const QCommandLineOption &plainOption,
    const QCommandLineOption &forceOption)
{
    if (positional.size() != 3)
        return usageError(QStringLiteral("linuxbeectl: playlist convert takes IN and OUT.\n%1")
                .arg(QLatin1String(kUsage)));
    const QString in = positional.at(1);
    const QString out = positional.at(2);
    const auto from = linuxbee::playlistFormatForFile(QFileInfo(in).fileName());
    if (!from)
        return usageError(notAPlaylist(QStringLiteral("IN"), in));
    const auto to = linuxbee::playlistFormatForFile(QFileInfo(out).fileName());
    if (!to)
        return usageError(notAPlaylist(QStringLiteral("OUT"), out));

    QFile source(in);
    if (!source.open(QIODevice::ReadOnly))
        return fileError(in, QStringLiteral("cannot be read"));
    const QByteArray data = source.readAll();
    source.close();

    // Y-CLI: the one path of OUT is made once; every check, the folder of --relative and the write
    // use that path. Never onto the file read, with --force as without it; onto another file that
    // is there only with --force; never through a link that leads to nothing.
    const WrittenPath target = writtenPath(out);
    if (!target.linkToNothing && sameFile(in, target.path))
        return fileError(out, QStringLiteral("this is the file being read"));
    if (!parser.isSet(forceOption) && (target.linkToNothing || somethingIsAt(target.path)))
        return fileError(out, QStringLiteral("the file is there already (--force writes over it)"));
    if (target.linkToNothing)
        return fileError(out, QStringLiteral("it is a link to nothing"));

    linuxbee::PlaylistWriteOptions options;
    options.extended = !parser.isSet(plainOption);
    options.relativePaths = parser.isSet(relativeOption);
    options.baseFolder = folderOf(target.path);
    const QList<linuxbee::PlaylistEntry> entries
        = linuxbee::parsePlaylist(data, *from, folderOf(in));
    const QByteArray bytes = linuxbee::writePlaylist(entries, *to, options);

    // Y-CLI: OUT is not left half-written.
    QSaveFile destination(target.path);
    if (!destination.open(QIODevice::WriteOnly) || destination.write(bytes) != bytes.size()
        || !destination.commit())
        return fileError(out, QStringLiteral("cannot be written"));

    QTextStream stream(stdout);
    stream << QStringLiteral("%1 entries").arg(int(entries.size())) << '\n';
    return 0;
}

int runPlaylist(const QStringList &arguments)
{
    QCommandLineParser parser;
    QCommandLineOption relativeOption(QStringLiteral("relative"),
        QStringLiteral("Write locations relative to the folder of OUT."));
    QCommandLineOption plainOption(
        QStringLiteral("plain"), QStringLiteral("Leave out the #EXT lines."));
    QCommandLineOption forceOption(
        QStringLiteral("force"), QStringLiteral("Write over an OUT that is there already."));
    parser.addOptions({ relativeOption, plainOption, forceOption });

    // parse(), not process(): process() exits 1 on an unknown option, and
    // Y-CLI requires 2. The parser expects the program name first.
    if (!parser.parse(QStringList { QStringLiteral("linuxbeectl playlist") } + arguments))
        return usageError(parser.errorText());

    const QStringList positional = parser.positionalArguments();
    if (positional.isEmpty())
        return usageError(QStringLiteral("linuxbeectl: playlist needs show or convert.\n%1")
                .arg(QLatin1String(kUsage)));
    const QString subcommand = positional.at(0);
    if (subcommand == QLatin1String("show")) {
        if (positional.size() != 2)
            return usageError(
                QStringLiteral("linuxbeectl: playlist show takes exactly one file.\n%1")
                    .arg(QLatin1String(kUsage)));
        return showPlaylist(positional.at(1));
    }
    if (subcommand == QLatin1String("convert"))
        return convertPlaylist(positional, parser, relativeOption, plainOption, forceOption);
    return usageError(QStringLiteral("linuxbeectl: playlist has no subcommand \"%1\".\n%2")
            .arg(subcommand, QLatin1String(kUsage)));
}

}

LINUXBEE_COMMAND(
    playlist, "playlist", "Show a playlist file, or convert one to another format.", runPlaylist)
