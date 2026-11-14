#include "convert/convertbatch.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <utility>

namespace linuxbee {

namespace {

// An absolute, clean path. Looks at the working directory only, not the disk.
QString cleanAbsolute(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

// The name without its last extension. A name with no extension keeps all of it.
QString stemOf(const QString &name)
{
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    return dot > 0 ? name.left(dot) : name;
}

}

QList<ConvertJob> planBatch(const QStringList &sources, const BatchOptions &options)
{
    const QString trimmed = options.outputFolder.trimmed();
    const QString folder = trimmed.isEmpty() ? QString() : cleanAbsolute(trimmed);
    const QString extension = fileExtension(options.settings.format);

    // B-DEST: no job writes over a source, nor over an earlier job's result.
    // Every source is reserved before any destination is chosen.
    QStringList cleanSources;
    QSet<QString> taken;
    for (const QString &source : sources) {
        const QString clean = cleanAbsolute(source);
        cleanSources.append(clean);
        taken.insert(clean);
    }

    QList<ConvertJob> jobs;
    for (const QString &source : std::as_const(cleanSources)) {
        const QFileInfo info(source);
        const QString dir = folder.isEmpty() ? info.absolutePath() : folder;
        const QString stem = stemOf(info.fileName());

        QString destination = QDir(dir).filePath(stem + QLatin1Char('.') + extension);
        for (int n = 1; taken.contains(destination); ++n)
            destination = QDir(dir).filePath(
                stem + QLatin1Char('_') + QString::number(n) + QLatin1Char('.') + extension);
        taken.insert(destination);

        ConvertJob job;
        job.source = source;
        job.destination = destination;
        job.settings = options.settings;
        job.ifExists = options.ifExists;
        job.copyTags = options.copyTags;
        job.copyPictures = options.copyPictures;
        job.tagOptions = options.tagOptions;
        job.ffmpegPath = options.ffmpegPath;
        jobs.append(job);
    }
    return jobs;
}

}
