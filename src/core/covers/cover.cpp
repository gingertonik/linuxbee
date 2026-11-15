#include "covers/cover.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace linuxbee {

namespace {

// CV-FILE: the picture extensions, in the order they rank (jpg before jpeg before png).
const QStringList kPictureExtensions
    = { QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png") };

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
        if (rank >= 0)
            found.append({ info.fileName(), info.completeBaseName().toLower(), rank });
    }
    return found;
}

}

// CV-EMBEDDED
int chooseEmbedded(const QList<Picture> &pictures)
{
    if (pictures.isEmpty())
        return -1;
    for (qsizetype i = 0; i < pictures.size(); ++i) {
        if (pictures.at(i).type.compare(QStringLiteral("Front Cover"), Qt::CaseInsensitive) == 0)
            return int(i);
    }
    return 0;
}

// CV-FILE
QStringList coverNames()
{
    return { QStringLiteral("cover"), QStringLiteral("folder"), QStringLiteral("front"),
        QStringLiteral("album"), QStringLiteral("albumart") };
}

// CV-FILE
QString coverFileIn(const QString &folder)
{
    if (folder.isEmpty() || !QDir(folder).exists())
        return QString();

    const QList<PictureFile> pictures = pictureFilesIn(folder);
    const QString dir = QDir(folder).absolutePath();
    for (const QString &name : coverNames()) {
        // Among the files with this name: the first extension in order, then the code point order
        // of the name.
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
    }

    // No named picture: a lone picture counts, two or more do not.
    if (pictures.size() == 1)
        return dir + QLatin1Char('/') + pictures.first().fileName;
    return QString();
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

// CV-FIND, file: the cover file in the music file's folder, CV-FILE.
Cover fileCover(const QString &musicFile, bool wantData)
{
    if (musicFile.isEmpty())
        return { };
    const QString picturePath = coverFileIn(QFileInfo(musicFile).absolutePath());
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
    for (const Cover::Source source : options.order) {
        Cover found;
        if (source == Cover::Source::Embedded)
            found = embeddedCover(musicFile, options.data);
        else if (source == Cover::Source::File)
            found = fileCover(musicFile, options.data);
        if (found.source != Cover::Source::None)
            return found;
    }
    return { };
}

}
