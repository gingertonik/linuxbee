#include "covers/cover.h"

#include <QDir>
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

// STUB (plan 37). The builder writes the real bodies here.
Cover findCover(const QString &, const CoverOptions &)
{
    return { };
}

}
