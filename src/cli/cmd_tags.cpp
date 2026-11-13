#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QTextStream>

#include "command.h"
#include "tags/tagreader.h"

namespace {

using linuxbee::cli::usageError;

const char *const kUsage = "usage: linuxbeectl tags read [--field NAME] FILE";

// R-CLI: a backslash is written \\, a line feed \n and a carriage return \r.
QString escaped(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar ch : text) {
        if (ch == QLatin1Char('\\'))
            out += QLatin1String("\\\\");
        else if (ch == QLatin1Char('\n'))
            out += QLatin1String("\\n");
        else if (ch == QLatin1Char('\r'))
            out += QLatin1String("\\r");
        else
            out += ch;
    }
    return out;
}

// R-CLI: the whole report, one line per item, in the order the spec gives.
void printReport(QTextStream &out, const linuxbee::TrackTags &tags)
{
    out << "format=" << escaped(tags.format) << '\n';
    out << "tags=" << escaped(tags.tagTypes.join(QLatin1Char(','))) << '\n';
    out << "duration_ms=" << tags.audio.durationMs << '\n';
    out << "bitrate_kbps=" << tags.audio.bitrateKbps << '\n';
    out << "sample_rate_hz=" << tags.audio.sampleRateHz << '\n';
    out << "channels=" << tags.audio.channels << '\n';
    out << "bits_per_sample=" << tags.audio.bitsPerSample << '\n';

    // Fields in catalogue order, then the other keys in ascending order.
    for (const QString &name : linuxbee::tagFieldNames()) {
        for (const QString &value : tags.fields.value(name))
            out << "field " << name << '=' << escaped(value) << '\n';
    }
    for (auto it = tags.other.cbegin(); it != tags.other.cend(); ++it) {
        for (const QString &value : it.value())
            out << "other " << it.key() << '=' << escaped(value) << '\n';
    }
    for (const QString &name : tags.unsupported)
        out << "unsupported " << escaped(name) << '\n';
    for (const linuxbee::Picture &picture : tags.pictures) {
        out << "picture type=" << escaped(picture.type) << " mime=" << escaped(picture.mimeType)
            << " bytes=" << picture.byteCount << " description=" << escaped(picture.description)
            << '\n';
    }
}

int runTags(const QStringList &arguments)
{
    QCommandLineParser parser;
    QCommandLineOption fieldOption(QStringLiteral("field"),
        QStringLiteral("Print only the values of this field, one per line."),
        QStringLiteral("NAME"));
    parser.addOptions({ fieldOption });

    // parse(), not process(): process() exits 1 on an unknown option, and R-CLI wants 2.
    if (!parser.parse(QStringList { QStringLiteral("linuxbeectl tags") } + arguments))
        return usageError(parser.errorText());

    const QStringList positional = parser.positionalArguments();
    if (positional.isEmpty())
        return usageError(
            QStringLiteral("linuxbeectl: tags needs read.\n%1").arg(QLatin1String(kUsage)));
    if (positional.at(0) != QLatin1String("read"))
        return usageError(QStringLiteral("linuxbeectl: tags has no subcommand \"%1\".\n%2")
                .arg(positional.at(0), QLatin1String(kUsage)));
    if (positional.size() != 2)
        return usageError(QStringLiteral("linuxbeectl: tags read takes exactly one file.\n%1")
                .arg(QLatin1String(kUsage)));

    QString field;
    if (parser.isSet(fieldOption)) {
        // R-CLI: NAME is matched ignoring case; a name that is not a field is a usage mistake.
        const QString requested = parser.value(fieldOption);
        for (const QString &name : linuxbee::tagFieldNames()) {
            if (name.compare(requested, Qt::CaseInsensitive) == 0)
                field = name;
        }
        if (field.isEmpty())
            return usageError(QStringLiteral("linuxbeectl: no field named \"%1\".").arg(requested));
    }

    const QString path = positional.at(1);
    linuxbee::ReadOptions options;
    options.pictureData = false; // R-CLI: picture data is not read.
    const linuxbee::ReadResult result = linuxbee::readTags(path, options);

    if (result.status != linuxbee::ReadResult::Status::Ok) {
        QTextStream err(stderr);
        err << (result.status == linuxbee::ReadResult::Status::NotFound ? "not found: "
                                                                        : "not a music file: ")
            << path << '\n';
        err.flush();
        return 3;
    }

    QTextStream out(stdout);
    if (field.isEmpty()) {
        printReport(out, result.tags);
    } else {
        for (const QString &value : result.tags.fields.value(field))
            out << escaped(value) << '\n';
    }
    out.flush();
    return 0;
}

}

LINUXBEE_COMMAND(
    tags, "tags", "Read the tags, pictures and audio properties of a music file.", runTags)
