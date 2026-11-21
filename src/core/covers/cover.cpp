#include "covers/cover.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace linuxbee {

namespace {

// CV-FILE: the picture extensions, in the order they rank (jpg before jpeg before png).
const QStringList kPictureExtensions
    = { QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png") };

// CV-FILE: the largest picture file that counts, 32 MiB in bytes; a file of exactly this size
// counts.
const qint64 kLargestPictureBytes = 32LL * 1024 * 1024;

// A picture file lying directly in a folder. Hidden files are never listed.
struct PictureFile {
    QString fileName; // as on disk
    QString base; // the name without its extension, lower-case
    int rank; // its place in kPictureExtensions
};

QList<PictureFile> pictureFilesIn(const QString &folder)
{
    QList<PictureFile> found;
    const QFileInfoList entries = QDir(folder).entryInfoList(QDir::Files);
    for (const QFileInfo &info : entries) {
        // CV-FILE: a link leads through to what it names; only a regular file counts.
        if (!info.isFile())
            continue;
        const int rank = int(kPictureExtensions.indexOf(info.suffix().toLower()));
        // CV-FILE: a file of no bytes, a file over the largest size, or one this user cannot read
        // is not a picture file.
        if (rank >= 0 && info.size() > 0 && info.size() <= kLargestPictureBytes
            && info.isReadable())
            found.append({ info.fileName(), info.completeBaseName().toLower(), rank });
    }
    return found;
}

// The path of the picture file with this name (case ignored): the first extension in order, then
// the code point order of the file name. Empty when there is none.
QString pictureNamed(const QList<PictureFile> &pictures, const QString &dir, const QString &name)
{
    const PictureFile *best = nullptr;
    for (const PictureFile &picture : pictures) {
        if (picture.base != name)
            continue;
        if (!best || picture.rank < best->rank
            || (picture.rank == best->rank && picture.fileName < best->fileName))
            best = &picture;
    }
    if (best)
        return dir + QLatin1Char('/') + best->fileName;
    return QString();
}

// CV-FILE: a lone picture counts, two or more do not.
QString lonePicture(const QList<PictureFile> &pictures, const QString &dir)
{
    if (pictures.size() == 1)
        return dir + QLatin1Char('/') + pictures.first().fileName;
    return QString();
}

// The names linuxBee adds after MusicBee's three (CV-FIND): the ones from this place on.
const int kMusicBeeNames = 3;

}

// CV-EMBEDDED
int chooseEmbedded(const QList<Picture> &pictures)
{
    if (pictures.isEmpty())
        return -1;
    // A picture of no bytes is passed over, as if it were not there, for the front cover and for
    // the first picture alike.
    for (qsizetype i = 0; i < pictures.size(); ++i) {
        if (pictures.at(i).byteCount > 0
            && pictures.at(i).type.compare(QStringLiteral("Front Cover"), Qt::CaseInsensitive) == 0)
            return int(i);
    }
    for (qsizetype i = 0; i < pictures.size(); ++i) {
        if (pictures.at(i).byteCount > 0)
            return int(i);
    }
    return -1;
}

// CV-FILE
QStringList coverNames()
{
    return { QStringLiteral("cover"), QStringLiteral("artwork"), QStringLiteral("front"),
        QStringLiteral("folder"), QStringLiteral("album"), QStringLiteral("albumart") };
}

// CV-FILE
QString coverFileIn(const QString &folder)
{
    if (folder.isEmpty() || !QDir(folder).exists())
        return QString();

    const QList<PictureFile> pictures = pictureFilesIn(folder);
    const QString dir = QDir(folder).absolutePath();
    for (const QString &name : coverNames()) {
        const QString named = pictureNamed(pictures, dir, name);
        if (!named.isEmpty())
            return named;
    }

    // No named picture: a lone picture counts, two or more do not.
    return lonePicture(pictures, dir);
}

namespace {

// CV-FIND, embedded: the picture the music file holds, chosen by CV-EMBEDDED.
Cover embeddedCover(const QString &musicFile, bool wantData)
{
    if (musicFile.isEmpty())
        return { };
    ReadOptions read;
    read.pictures = true;
    read.pictureData = wantData;
    const ReadResult result = readTags(musicFile, read);
    if (result.status != ReadResult::Status::Ok)
        return { };
    const int chosen = chooseEmbedded(result.tags.pictures);
    if (chosen < 0)
        return { };

    const Picture &picture = result.tags.pictures.at(chosen);
    Cover cover;
    cover.source = Cover::Source::Embedded;
    cover.file = musicFile;
    cover.mimeType = picture.mimeType;
    cover.type = picture.type;
    cover.byteCount = picture.byteCount;
    cover.data = wantData ? picture.data : QByteArray();
    return cover;
}

// Which of the folder's pictures a File step may answer with.
enum class FileStep {
    MusicBees, // cover, artwork, front, the music file's own name
    Own, // linuxBee's own: folder, album, albumart, the lone picture
    Both, // MusicBee's, then linuxBee's
};

// CV-FIND, file: the cover file in the music file's folder, CV-FILE, as far as the step goes.
Cover fileCover(const QString &musicFile, bool wantData, FileStep step)
{
    if (musicFile.isEmpty())
        return { };
    const QFileInfo musicInfo(musicFile);
    const QString folder = musicInfo.absolutePath();
    if (!QDir(folder).exists())
        return { };

    const QList<PictureFile> pictures = pictureFilesIn(folder);
    const QString dir = QDir(folder).absolutePath();
    const QStringList names = coverNames();
    QString picturePath;
    if (step != FileStep::Own) {
        for (int i = 0; i < kMusicBeeNames && picturePath.isEmpty(); ++i)
            picturePath = pictureNamed(pictures, dir, names.at(i));
        if (picturePath.isEmpty())
            picturePath = pictureNamed(pictures, dir, musicInfo.completeBaseName().toLower());
    }
    if (picturePath.isEmpty() && step != FileStep::MusicBees) {
        for (int i = kMusicBeeNames; i < names.size() && picturePath.isEmpty(); ++i)
            picturePath = pictureNamed(pictures, dir, names.at(i));
        if (picturePath.isEmpty())
            picturePath = lonePicture(pictures, dir);
    }
    if (picturePath.isEmpty())
        return { };

    const QFileInfo info(picturePath);
    if (!info.isFile())
        return { }; // the folder changed since it was listed
    Cover cover;
    cover.source = Cover::Source::File;
    cover.file = picturePath;
    cover.mimeType = info.suffix().toLower() == QLatin1String("png") ? QStringLiteral("image/png")
                                                                     : QStringLiteral("image/jpeg");
    cover.byteCount = qsizetype(info.size());
    if (wantData) {
        QFile file(picturePath);
        if (!file.open(QIODevice::ReadOnly))
            return { }; // a file that cannot be read is not a cover (the spec is silent here)
        cover.data = file.readAll();
    }
    return cover;
}

}

// CV-FIND: the sources in the order given; the first that has a cover answers.
Cover findCover(const QString &musicFile, const CoverOptions &options)
{
    // The File step: with fallbackLast, MusicBee's names only; linuxBee's own come after the last
    // source. Otherwise both at once.
    const FileStep fileStep = options.fallbackLast ? FileStep::MusicBees : FileStep::Both;
    for (const Cover::Source source : options.order) {
        Cover found;
        if (source == Cover::Source::Embedded)
            found = embeddedCover(musicFile, options.data);
        else if (source == Cover::Source::File)
            found = fileCover(musicFile, options.data, fileStep);
        if (found.source != Cover::Source::None)
            return found;
    }
    if (options.fallbackLast && options.order.contains(Cover::Source::File))
        return fileCover(musicFile, options.data, FileStep::Own);
    return { };
}

}
