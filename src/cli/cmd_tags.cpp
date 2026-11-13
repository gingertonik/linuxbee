#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include "command.h"
#include "tags/tagreader.h"
#include "tags/tagwriter.h"

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

const char *const kWriteUsage
    = "usage: linuxbeectl tags write [--set NAME=VALUE]... [--clear NAME]... "
      "[--set-other KEY=VALUE]... [--clear-other KEY]... [--picture IMAGE | --no-pictures] "
      "[--id3v2.4] [--no-id3v1] [--keep-mtime] FILE";

// W-CLI: "NAME=VALUE" is split at the first '='. False when there is no '='.
bool splitPair(const QString &text, QString &name, QString &value)
{
    const qsizetype equals = text.indexOf(QLatin1Char('='));
    if (equals < 0)
        return false;
    name = text.left(equals);
    value = text.mid(equals + 1);
    return true;
}

// W-CLI: linuxbeectl tags write. Saves with writeTags and reports its result.
int runWrite(const QStringList &arguments)
{
    QCommandLineParser parser;
    QCommandLineOption setOption(QStringLiteral("set"),
        QStringLiteral("Set a field to NAME=VALUE. Repeat for several values."),
        QStringLiteral("NAME=VALUE"));
    QCommandLineOption clearOption(
        QStringLiteral("clear"), QStringLiteral("Remove a field."), QStringLiteral("NAME"));
    QCommandLineOption setOtherOption(QStringLiteral("set-other"),
        QStringLiteral("Set another tag to KEY=VALUE. Repeat for several values."),
        QStringLiteral("KEY=VALUE"));
    QCommandLineOption clearOtherOption(QStringLiteral("clear-other"),
        QStringLiteral("Remove another tag."), QStringLiteral("KEY"));
    QCommandLineOption pictureOption(QStringLiteral("picture"),
        QStringLiteral("Make IMAGE the only picture, a front cover."), QStringLiteral("IMAGE"));
    QCommandLineOption noPicturesOption(
        QStringLiteral("no-pictures"), QStringLiteral("Remove every picture."));
    QCommandLineOption id3v24Option(
        QStringLiteral("id3v2.4"), QStringLiteral("Save ID3v2.4 instead of ID3v2.3."));
    QCommandLineOption noId3v1Option(
        QStringLiteral("no-id3v1"), QStringLiteral("Do not keep an ID3v1 tag."));
    QCommandLineOption keepMtimeOption(
        QStringLiteral("keep-mtime"), QStringLiteral("Keep the file's modification time."));
    parser.addOptions({ setOption, clearOption, setOtherOption, clearOtherOption, pictureOption,
        noPicturesOption, id3v24Option, noId3v1Option, keepMtimeOption });

    // parse(), not process(): a usage mistake exits 2, as R-CLI wants.
    if (!parser.parse(QStringList { QStringLiteral("linuxbeectl tags") } + arguments))
        return usageError(parser.errorText());

    // The positional list starts with "write".
    const QStringList positional = parser.positionalArguments();
    if (positional.size() != 2)
        return usageError(QStringLiteral("linuxbeectl: tags write takes exactly one file.\n%1")
                .arg(QLatin1String(kWriteUsage)));
    const QString path = positional.at(1);

    if (parser.isSet(pictureOption) && parser.isSet(noPicturesOption))
        return usageError(
            QStringLiteral("linuxbeectl: give --picture or --no-pictures, not both.\n%1")
                .arg(QLatin1String(kWriteUsage)));

    // A clear is applied first, so a set of the same name keeps its values.
    linuxbee::TagChanges changes;
    for (const QString &name : parser.values(clearOption))
        changes.fields.insert(name, QStringList());
    for (const QString &key : parser.values(clearOtherOption))
        changes.other.insert(key, QStringList());
    for (const QString &pair : parser.values(setOption)) {
        QString name;
        QString value;
        if (!splitPair(pair, name, value))
            return usageError(QStringLiteral("linuxbeectl: --set needs NAME=VALUE, got \"%1\".\n%2")
                    .arg(pair, QLatin1String(kWriteUsage)));
        changes.fields[name].append(value);
    }
    for (const QString &pair : parser.values(setOtherOption)) {
        QString key;
        QString value;
        if (!splitPair(pair, key, value))
            return usageError(
                QStringLiteral("linuxbeectl: --set-other needs KEY=VALUE, got \"%1\".\n%2")
                    .arg(pair, QLatin1String(kWriteUsage)));
        changes.other[key].append(value);
    }

    if (parser.isSet(noPicturesOption))
        changes.pictures = QList<linuxbee::Picture> { };
    if (parser.isSet(pictureOption)) {
        const QString image = parser.value(pictureOption);
        QFile file(image);
        if (!QFileInfo(image).isFile() || !file.open(QIODevice::ReadOnly)) {
            QTextStream err(stderr);
            err << "not found: " << image << '\n';
            err.flush();
            return 3;
        }
        const QByteArray data = file.readAll();
        const QString mimeType = linuxbee::imageMimeType(data);
        if (mimeType.isEmpty())
            return usageError(
                QStringLiteral("linuxbeectl: not an image this tag can hold: %1").arg(image));
        linuxbee::Picture picture;
        picture.type = QStringLiteral("Front Cover");
        picture.mimeType = mimeType;
        picture.data = data;
        picture.byteCount = data.size();
        changes.pictures = QList<linuxbee::Picture> { picture };
    }

    linuxbee::WriteOptions options;
    options.id3Version = parser.isSet(id3v24Option) ? linuxbee::WriteOptions::Id3Version::V24
                                                    : linuxbee::WriteOptions::Id3Version::V23;
    options.writeId3v1 = !parser.isSet(noId3v1Option);
    options.keepModificationTime = parser.isSet(keepMtimeOption);

    const linuxbee::WriteResult result = linuxbee::writeTags(path, changes, options);
    QTextStream out(stdout);
    QTextStream err(stderr);
    int code = 3;
    switch (result.status) {
    case linuxbee::WriteResult::Status::Ok:
        out << "saved\n";
        code = 0;
        break;
    case linuxbee::WriteResult::Status::Unchanged:
        out << "unchanged\n";
        code = 0;
        break;
    case linuxbee::WriteResult::Status::InvalidChange:
        for (const QString &problem : result.problems)
            err << problem << '\n';
        code = 2;
        break;
    case linuxbee::WriteResult::Status::NotFound:
        err << "not found: " << path << '\n';
        break;
    case linuxbee::WriteResult::Status::NotAudio:
        err << "not a music file: " << path << '\n';
        break;
    case linuxbee::WriteResult::Status::NotWritable:
        err << "not writable: " << path << '\n';
        break;
    case linuxbee::WriteResult::Status::Failed:
        err << "could not save: " << path << '\n';
        break;
    }
    out.flush();
    err.flush();
    return code;
}

int runTags(const QStringList &arguments)
{
    if (!arguments.isEmpty() && arguments.first() == QLatin1String("write"))
        return runWrite(arguments);

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
