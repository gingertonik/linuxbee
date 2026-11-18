#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QSet>
#include <QStringList>
#include <QTextStream>

#include <optional>

#include "command.h"
#include "convert/converter.h"
#include "fields/trackfields.h"
#include "paths/pathbuilder.h"
#include "tags/tagreader.h"
#include "template/template.h"

namespace {

using linuxbee::AudioFormat;
using linuxbee::ConvertJob;
using linuxbee::ConvertResult;
using linuxbee::EncoderSettings;
using linuxbee::cli::usageError;

const char *const kUsage = "usage: linuxbeectl convert --list-presets\n"
                           "       linuxbeectl convert [--preset ID | --format F] [--bitrate K] "
                           "[--quality Q]\n"
                           "                           [--level L] [--sample-rate HZ] "
                           "[--bit-depth N] [--channels N]\n"
                           "                           (--output FILE | --out-dir DIR "
                           "[--template T])\n"
                           "                           [--if-exists skip|overwrite|rename|"
                           "update-tags]\n"
                           "                           [--no-tags] [--no-pictures] "
                           "[--ffmpeg PATH] FILE...";

// A usage mistake: exit 2, nothing converted, nothing on standard output.
int badUsage(const QString &message)
{
    return usageError(QStringLiteral("linuxbeectl: %1\n%2").arg(message, QLatin1String(kUsage)));
}

// C-CLI: a template that does not parse prints "error at N: message".
int printParseError(const linuxbee::TemplateError &error)
{
    QTextStream err(stderr);
    err << QStringLiteral("error at %1: %2").arg(error.position).arg(error.message) << '\n';
    err.flush();
    return 2;
}

// C-CLI: where --format starts. MP3 keeps its 320 kbps default; the lossless
// formats have nothing to start from.
EncoderSettings formatStart(AudioFormat format)
{
    EncoderSettings settings;
    settings.format = format;
    if (format == AudioFormat::OggVorbis) {
        settings.rateControl = EncoderSettings::RateControl::Quality;
        settings.quality = 5;
    } else if (format == AudioFormat::Opus || format == AudioFormat::Aac) {
        settings.bitrateKbps = 192;
    }
    return settings;
}

bool parseInt(const QString &text, int *value)
{
    bool ok = false;
    const int number = text.toInt(&ok);
    if (ok)
        *value = number;
    return ok;
}

bool parseDouble(const QString &text, double *value)
{
    bool ok = false;
    const double number = text.toDouble(&ok);
    if (ok)
        *value = number;
    return ok;
}

QString joinPath(const QString &folder, const QString &relative)
{
    if (folder.endsWith(QLatin1Char('/')))
        return folder + relative;
    return folder + QLatin1Char('/') + relative;
}

// C-OWN: the path two files are compared by. The real path when the file is
// there; otherwise the real folder with the file name.
QString comparablePath(const QString &path)
{
    const QFileInfo info(path);
    const QString real = info.canonicalFilePath();
    if (!real.isEmpty())
        return real;
    const QString folder = QFileInfo(info.absolutePath()).canonicalFilePath();
    if (folder.isEmpty())
        return info.absoluteFilePath();
    return folder + QLatin1Char('/') + info.fileName();
}

QString failureReason(const ConvertResult &result)
{
    switch (result.status) {
    case ConvertResult::Status::SourceNotFound:
        return QStringLiteral("not found");
    case ConvertResult::Status::SourceNotAudio:
        return QStringLiteral("not a music file");
    case ConvertResult::Status::Cancelled:
        return QStringLiteral("cancelled");
    default:
        break;
    }
    return result.message.isEmpty() ? QStringLiteral("failed") : result.message;
}

int runConvert(const QStringList &arguments)
{
    QCommandLineParser parser;
    QCommandLineOption listPresets(QStringLiteral("list-presets"),
        QStringLiteral("Print one line per preset: id, a tab, name."));
    QCommandLineOption preset(
        QStringLiteral("preset"), QStringLiteral("Start from preset ID."), QStringLiteral("ID"));
    QCommandLineOption format(
        QStringLiteral("format"), QStringLiteral("Start from format F."), QStringLiteral("F"));
    QCommandLineOption bitrate(QStringLiteral("bitrate"),
        QStringLiteral("Bitrate in kbps; selects bitrate control."), QStringLiteral("K"));
    QCommandLineOption quality(QStringLiteral("quality"),
        QStringLiteral("Quality; selects quality control."), QStringLiteral("Q"));
    QCommandLineOption level(QStringLiteral("level"),
        QStringLiteral("FLAC compression level, 0 to 12."), QStringLiteral("L"));
    QCommandLineOption sampleRate(QStringLiteral("sample-rate"),
        QStringLiteral("Sample rate in Hz; 0 keeps the source's."), QStringLiteral("HZ"));
    QCommandLineOption bitDepth(QStringLiteral("bit-depth"),
        QStringLiteral("Bit depth for a lossless format: 16, 24 or 32."), QStringLiteral("N"));
    QCommandLineOption channels(QStringLiteral("channels"),
        QStringLiteral("Channels: 1 or 2; 0 keeps the source's."), QStringLiteral("N"));
    QCommandLineOption output(QStringLiteral("output"),
        QStringLiteral("The file to create, for one input."), QStringLiteral("FILE"));
    QCommandLineOption outDir(QStringLiteral("out-dir"),
        QStringLiteral("The folder to create each file in."), QStringLiteral("DIR"));
    QCommandLineOption templateOption(QStringLiteral("template"),
        QStringLiteral("Naming template for the file under --out-dir."), QStringLiteral("T"));
    QCommandLineOption ifExists(QStringLiteral("if-exists"),
        QStringLiteral("What to do when the file exists: skip (default), overwrite, rename "
                       "or update-tags."),
        QStringLiteral("WORD"));
    QCommandLineOption noTags(QStringLiteral("no-tags"), QStringLiteral("Do not copy the tags."));
    QCommandLineOption noPictures(
        QStringLiteral("no-pictures"), QStringLiteral("Do not copy the pictures."));
    QCommandLineOption ffmpegPath(QStringLiteral("ffmpeg"),
        QStringLiteral("The ffmpeg program to run."), QStringLiteral("PATH"));
    parser.addOptions({ listPresets, preset, format, bitrate, quality, level, sampleRate, bitDepth,
        channels, output, outDir, templateOption, ifExists, noTags, noPictures, ffmpegPath });

    // parse(), not process(): process() exits 1 on an unknown option, and a
    // usage mistake exits 2. The parser expects the program name first.
    if (!parser.parse(QStringList { QStringLiteral("linuxbeectl convert") } + arguments))
        return badUsage(parser.errorText());

    QTextStream out(stdout);
    QTextStream err(stderr);

    if (parser.isSet(listPresets)) {
        for (const linuxbee::EncoderPreset &item : linuxbee::encoderPresets())
            out << item.id << '\t' << item.name << '\n';
        out.flush();
        return 0;
    }

    const QStringList inputs = parser.positionalArguments();
    if (inputs.isEmpty())
        return badUsage(QStringLiteral("convert needs at least one FILE."));
    if (parser.isSet(preset) && parser.isSet(format))
        return badUsage(QStringLiteral("--preset and --format cannot be used together."));

    // Settings start from the first preset, from --preset, or from --format.
    EncoderSettings settings = linuxbee::encoderPresets().first().settings;
    if (parser.isSet(preset)) {
        const std::optional<linuxbee::EncoderPreset> chosen
            = linuxbee::encoderPreset(parser.value(preset));
        if (!chosen)
            return badUsage(QStringLiteral("no preset \"%1\".").arg(parser.value(preset)));
        settings = chosen->settings;
    } else if (parser.isSet(format)) {
        const std::optional<AudioFormat> chosen = linuxbee::formatFromId(parser.value(format));
        if (!chosen)
            return badUsage(QStringLiteral("no format \"%1\".").arg(parser.value(format)));
        settings = formatStart(*chosen);
    }

    // The number options replace single settings.
    if (parser.isSet(bitrate)) {
        if (!parseInt(parser.value(bitrate), &settings.bitrateKbps))
            return badUsage(QStringLiteral("--bitrate needs a number."));
        settings.rateControl = EncoderSettings::RateControl::Bitrate;
    }
    if (parser.isSet(quality)) {
        if (!parseDouble(parser.value(quality), &settings.quality))
            return badUsage(QStringLiteral("--quality needs a number."));
        settings.rateControl = EncoderSettings::RateControl::Quality;
    }
    if (parser.isSet(level) && !parseInt(parser.value(level), &settings.compressionLevel))
        return badUsage(QStringLiteral("--level needs a number."));
    if (parser.isSet(sampleRate) && !parseInt(parser.value(sampleRate), &settings.sampleRateHz))
        return badUsage(QStringLiteral("--sample-rate needs a number."));
    if (parser.isSet(bitDepth) && !parseInt(parser.value(bitDepth), &settings.bitDepth))
        return badUsage(QStringLiteral("--bit-depth needs a number."));
    if (parser.isSet(channels) && !parseInt(parser.value(channels), &settings.channels))
        return badUsage(QStringLiteral("--channels needs a number."));

    ConvertJob::IfExists policy = ConvertJob::IfExists::Skip;
    if (parser.isSet(ifExists)) {
        const QString word = parser.value(ifExists);
        if (word == QLatin1String("skip"))
            policy = ConvertJob::IfExists::Skip;
        else if (word == QLatin1String("overwrite"))
            policy = ConvertJob::IfExists::Overwrite;
        else if (word == QLatin1String("rename"))
            policy = ConvertJob::IfExists::Rename;
        else if (word == QLatin1String("update-tags"))
            policy = ConvertJob::IfExists::UpdateTags;
        else
            return badUsage(
                QStringLiteral("--if-exists takes skip, overwrite, rename or update-tags."));
    }

    const QStringList problems = linuxbee::validateSettings(settings);
    if (!problems.isEmpty())
        return badUsage(problems.join(QStringLiteral("; ")));

    const bool toFile = parser.isSet(output);
    const bool toFolder = parser.isSet(outDir);
    if (toFile == toFolder)
        return badUsage(QStringLiteral("give either --output FILE or --out-dir DIR."));
    if (toFile && inputs.size() != 1)
        return badUsage(
            QStringLiteral("--output takes exactly one FILE; use --out-dir for several."));
    if (parser.isSet(templateOption) && !toFolder)
        return badUsage(QStringLiteral("--template needs --out-dir."));

    std::optional<linuxbee::Template> naming;
    if (parser.isSet(templateOption)) {
        const linuxbee::Template parsed = linuxbee::Template::parse(parser.value(templateOption));
        if (!parsed.isValid())
            return printParseError(parsed.error());
        naming = parsed;
    }

    const QString folder = toFolder ? parser.value(outDir) : QString();
    const QString extension = linuxbee::fileExtension(settings.format);
    const linuxbee::PathRules rules;
    int failures = 0;

    const auto fail = [&](const QString &input, const QString &reason) {
        err << QStringLiteral("failed: %1: %2").arg(input, reason) << '\n';
        err.flush();
        ++failures;
    };

    // C-OWN: with --out-dir every input is taken before any destination is
    // chosen, and each chosen destination is taken in turn. A path is known by
    // its real folder, so a folder named through a link is the same folder.
    QSet<QString> taken;
    if (toFolder) {
        for (const QString &input : inputs)
            taken.insert(comparablePath(input));
    }

    for (const QString &input : inputs) {
        ConvertJob job;
        job.source = input;
        job.settings = settings;
        job.ifExists = policy;
        job.copyTags = !parser.isSet(noTags);
        job.copyPictures = !parser.isSet(noPictures);
        if (parser.isSet(ffmpegPath))
            job.ffmpegPath = parser.value(ffmpegPath);

        if (toFile) {
            job.destination = parser.value(output);
        } else if (naming) {
            const linuxbee::ReadResult read = linuxbee::readTags(input);
            if (read.status == linuxbee::ReadResult::Status::NotFound) {
                fail(input, QStringLiteral("not found"));
                continue;
            }
            if (read.status == linuxbee::ReadResult::Status::NotAudio) {
                fail(input, QStringLiteral("not a music file"));
                continue;
            }
            // C-CLI: the template is filled from the track fields, the computed
            // ones included (Disc-Track#), with the file's own path for the file fields.
            const QString filePath = QFileInfo(input).absoluteFilePath();
            const linuxbee::TrackFieldSource fields(read.tags, filePath);
            const linuxbee::BuiltPath built
                = linuxbee::buildRelativePath(*naming, fields, extension, rules);
            if (built.status == linuxbee::BuiltPath::Status::MissingTags) {
                fail(input,
                    QStringLiteral("missing tags: ")
                        + built.missingFields.join(QStringLiteral(", ")));
                continue;
            }
            if (built.status != linuxbee::BuiltPath::Status::Ok) {
                fail(input, QStringLiteral("the template leaves no file name"));
                continue;
            }
            job.destination = joinPath(folder, built.relativePath);
        } else {
            job.destination = joinPath(
                folder, QFileInfo(input).completeBaseName() + QLatin1Char('.') + extension);
        }

        if (toFolder) {
            const QString free = linuxbee::uniqueRelativePath(job.destination,
                [&taken](const QString &path) { return taken.contains(comparablePath(path)); });
            if (free.isEmpty()) {
                fail(input, QStringLiteral("no free file name is left"));
                continue;
            }
            job.destination = free;
            taken.insert(comparablePath(free));
        }

        const ConvertResult result = linuxbee::convertFile(job);
        for (const QString &warning : result.warnings)
            err << QStringLiteral("warning: %1: %2").arg(input, warning) << '\n';
        err.flush();

        switch (result.status) {
        case ConvertResult::Status::Converted:
            out << QStringLiteral("converted: %1 -> %2").arg(input, result.outputPath) << '\n';
            break;
        case ConvertResult::Status::Skipped:
            out << QStringLiteral("skipped: %1 -> %2").arg(input, result.outputPath) << '\n';
            break;
        case ConvertResult::Status::TagsUpdated:
            out << QStringLiteral("tags updated: %1 -> %2").arg(input, result.outputPath) << '\n';
            break;
        default:
            fail(input, failureReason(result));
            break;
        }
        out.flush();
    }

    return failures == 0 ? 0 : 3;
}

}

LINUXBEE_COMMAND(convert, "convert", "Convert music files to another format.", runConvert)
