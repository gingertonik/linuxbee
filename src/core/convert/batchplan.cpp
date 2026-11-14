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

// The part file convertFile writes first (convert.md C-CLEAN): ".part" before
// the extension. Every name built here has an extension.
QString partNameOf(const QString &stem, const QString &extension)
{
    return stem + QStringLiteral(".part.") + extension;
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

        // A job writes its part file as well as its destination: neither may be a
        // source or a path an earlier job writes.
        QString name = stem;
        for (int n = 1; taken.contains(QDir(dir).filePath(name + QLatin1Char('.') + extension))
            || taken.contains(QDir(dir).filePath(partNameOf(name, extension)));
            ++n)
            name = stem + QLatin1Char('_') + QString::number(n);
        const QString destination = QDir(dir).filePath(name + QLatin1Char('.') + extension);
        taken.insert(destination);
        taken.insert(QDir(dir).filePath(partNameOf(name, extension)));

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
