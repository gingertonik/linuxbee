#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QTextStream>

#include <optional>
#include <vector>

#include "command.h"
#include "sound/replaygain.h"
#include "tags/tagreader.h"
#include "tags/tagwriter.h"

namespace {

using linuxbee::cli::usageError;

const char *const kUsage = "usage: linuxbeectl gain measure [--album] FILE...\n"
                           "       linuxbeectl gain write [--album] FILE...\n"
                           "       linuxbeectl gain remove FILE...";

// N-CLI: exit 3 when a file cannot be measured, read or saved.
constexpr int kFailed = 3;

void reportFailure(const QString &file, const QString &reason)
{
    QTextStream err(stderr);
    err << QStringLiteral("failed: %1: %2").arg(file, reason) << '\n';
    err.flush();
}

QString writeReason(const linuxbee::WriteResult &result)
{
    using Status = linuxbee::WriteResult::Status;
    switch (result.status) {
    case Status::Ok:
        return QStringLiteral("saved");
    case Status::Unchanged:
        return QStringLiteral("unchanged");
    case Status::NotFound:
        return QStringLiteral("not found");
    case Status::NotAudio:
        return QStringLiteral("not an audio file");
    case Status::NotWritable:
        return QStringLiteral("not writable");
    case Status::InvalidChange:
        return result.problems.isEmpty() ? QStringLiteral("the change was refused")
                                         : result.problems.join(QStringLiteral("; "));
    case Status::Failed:
        return QStringLiteral("the tag library could not save");
    }
    return QStringLiteral("unknown status");
}

// "+3.80 dB, peak 0.088105", as the gain tags carry it. kind is TRACK or ALBUM.
QString figures(const linuxbee::TagChanges &changes, const QString &kind)
{
    const QString gain
        = changes.other.value(QStringLiteral("REPLAYGAIN_%1_GAIN").arg(kind)).value(0);
    const QString peak
        = changes.other.value(QStringLiteral("REPLAYGAIN_%1_PEAK").arg(kind)).value(0);
    return QStringLiteral("%1, peak %2").arg(gain, peak);
}

// measure and write. Every file is measured before anything is printed or
// saved: one file that cannot be measured leaves every file as it was.
int measureOrWrite(bool write, bool album, const QStringList &files)
{
    bool failed = false;
    std::vector<linuxbee::Loudness> tracks;
    for (const QString &file : files) {
        const linuxbee::Loudness track = linuxbee::measureLoudness({ file });
        if (!track.ok) {
            reportFailure(file, track.error);
            failed = true;
            continue;
        }
        tracks.push_back(track);
    }

    std::optional<linuxbee::Loudness> albumLoudness;
    if (album && !failed) {
        const linuxbee::Loudness measured = linuxbee::measureLoudness(files);
        if (!measured.ok) {
            reportFailure(QStringLiteral("album"), measured.error);
            failed = true;
        } else {
            albumLoudness = measured;
        }
    }
    if (failed)
        return kFailed;

    QTextStream out(stdout);
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        out << files.at(static_cast<qsizetype>(i)) << ": track "
            << figures(linuxbee::gainTagChanges(tracks[i], std::nullopt), QStringLiteral("TRACK"))
            << '\n';
    }
    if (albumLoudness) {
        out << "album: "
            << figures(
                   linuxbee::gainTagChanges(std::nullopt, albumLoudness), QStringLiteral("ALBUM"))
            << '\n';
    }
    out.flush();

    if (!write)
        return 0;

    for (std::size_t i = 0; i < tracks.size(); ++i) {
        const QString &file = files.at(static_cast<qsizetype>(i));
        const linuxbee::WriteResult result
            = linuxbee::writeTags(file, linuxbee::gainTagChanges(tracks[i], albumLoudness));
        if (!result.ok()) {
            reportFailure(file, writeReason(result));
            failed = true;
        }
    }
    return failed ? kFailed : 0;
}

int removeGain(const QStringList &files)
{
    QTextStream out(stdout);
    bool failed = false;
    linuxbee::ReadOptions readOptions;
    readOptions.pictures = false;
    for (const QString &file : files) {
        const linuxbee::ReadResult read = linuxbee::readTags(file, readOptions);
        if (read.status != linuxbee::ReadResult::Status::Ok) {
            reportFailure(file,
                read.status == linuxbee::ReadResult::Status::NotFound
                    ? QStringLiteral("not found")
                    : QStringLiteral("not an audio file"));
            failed = true;
            continue;
        }
        const linuxbee::TagChanges changes = linuxbee::removeGainTagChanges(read.tags);
        if (!changes.isEmpty()) {
            const linuxbee::WriteResult result = linuxbee::writeTags(file, changes);
            if (!result.ok()) {
                reportFailure(file, writeReason(result));
                failed = true;
                continue;
            }
        }
        out << file << ": " << changes.other.size() << " removed\n";
    }
    out.flush();
    return failed ? kFailed : 0;
}

int runGain(const QStringList &arguments)
{
    QCommandLineParser parser;
    QCommandLineOption albumOption(QStringLiteral("album"),
        QStringLiteral("Measure the files as one album, and write the album tags with write."));
    parser.addOptions({ albumOption });

    // parse(), not process(): process() exits 1 on an unknown option, and N-CLI requires 2.
    if (!parser.parse(QStringList { QStringLiteral("linuxbeectl gain") } + arguments))
        return usageError(QStringLiteral("%1\n%2").arg(parser.errorText(), QLatin1String(kUsage)));

    const QStringList positional = parser.positionalArguments();
    if (positional.isEmpty())
        return usageError(QStringLiteral("linuxbeectl: gain needs measure, write or remove.\n%1")
                .arg(QLatin1String(kUsage)));
    const QString subcommand = positional.at(0);
    const QStringList files = positional.mid(1);
    const bool album = parser.isSet(albumOption);

    const bool known = subcommand == QLatin1String("measure")
        || subcommand == QLatin1String("write") || subcommand == QLatin1String("remove");
    if (!known)
        return usageError(QStringLiteral("linuxbeectl: gain has no subcommand \"%1\".\n%2")
                .arg(subcommand, QLatin1String(kUsage)));
    if (files.isEmpty())
        return usageError(QStringLiteral("linuxbeectl: gain %1 needs at least one file.\n%2")
                .arg(subcommand, QLatin1String(kUsage)));
    if (subcommand == QLatin1String("remove")) {
        if (album)
            return usageError(QStringLiteral("linuxbeectl: gain remove takes no --album.\n%1")
                    .arg(QLatin1String(kUsage)));
        return removeGain(files);
    }
    return measureOrWrite(subcommand == QLatin1String("write"), album, files);
}

}

LINUXBEE_COMMAND(
    gain, "gain", "Measure, write or remove volume levelling (ReplayGain) tags.", runGain)
