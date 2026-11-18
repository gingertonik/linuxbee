#include "tagundo/tagundo.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSet>

#include <optional>

// Contract: docs/spec/tag-undo.md (UN-RECORD, UN-STEP).
namespace linuxbee {

namespace {

// What one kept picture was, without its bytes: those are in a file of the
// scratch folder, named by a hash of them.
struct KeptPicture {
    QString type;
    QString mimeType;
    QString description;
    QString fileName;
};

// What the entries one file's changes named held before the first write that named them.
struct KeptFile {
    QString path;
    QMap<QString, QStringList> fields;
    QMap<QString, QStringList> other;
    std::optional<QList<KeptPicture>> pictures;
};

struct Step {
    QString name;
    QList<KeptFile> files;

    KeptFile *find(const QString &path)
    {
        for (KeptFile &file : files) {
            if (file.path == path)
                return &file;
        }
        return nullptr;
    }
};

WriteResult refused(const QString &problem)
{
    WriteResult result;
    result.status = WriteResult::Status::Failed;
    result.problems = QStringList({ problem });
    return result;
}

}

struct TagUndo::Private {
    QString scratch;
    bool open = false;
    Step pending; // the step being made between begin and end
    Step committed; // the step undo would take back
    QSet<QString> made; // picture files this object made in the scratch folder, by name

    QSet<QString> referenced() const;
    void sweep();
    std::optional<QString> keepPicture(const QByteArray &data);
    bool restorePicture(const KeptPicture &kept, Picture *out) const;
};

// The picture files that a step still names.
QSet<QString> TagUndo::Private::referenced() const
{
    QSet<QString> names;
    for (const Step *step : { &pending, &committed }) {
        for (const KeptFile &file : step->files) {
            if (!file.pictures)
                continue;
            for (const KeptPicture &picture : *file.pictures)
                names.insert(picture.fileName);
        }
    }
    return names;
}

// Removes the files this object made that no step names any more. Nothing
// else in the folder is looked at, and the folder is left.
void TagUndo::Private::sweep()
{
    const QSet<QString> needed = referenced();
    const QSet<QString> mine = made;
    for (const QString &name : mine) {
        if (needed.contains(name))
            continue;
        QFile::remove(QDir(scratch).filePath(name));
        made.remove(name);
    }
}

// Puts one picture's bytes in a file named by their hash, unless it is there
// already. Returns the file's name, or nothing when it could not be kept.
std::optional<QString> TagUndo::Private::keepPicture(const QByteArray &data)
{
    const QString name
        = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex())
        + QStringLiteral(".picture");
    if (made.contains(name))
        return name; // this object made it, for this or the other step
    const QDir folder(scratch);
    if (QFileInfo(scratch).isSymLink() || (!folder.exists() && !QDir().mkpath(scratch))
        || !QFileInfo(scratch).isDir())
        return std::nullopt;
    const QString target = folder.filePath(name);
    QFile file(target);
    // NewOnly: never writes over a name that is there, a link included.
    if (file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        made.insert(
            name); // a file this object made from here on, so a failed write below is cleaned up
        const bool wrote = file.write(data) == data.size() && file.flush();
        file.close();
        if (!wrote || file.error() != QFileDevice::NoError
            || QFileInfo(target).size() != data.size()) {
            QFile::remove(target);
            made.remove(name);
            return std::nullopt;
        }
        return name;
    }
    // Somebody else's file has the name: usable, and not ours to remove, only when it holds these
    // very bytes.
    const QFileInfo info(target);
    if (info.isSymLink() || !info.isFile() || info.size() != data.size())
        return std::nullopt;
    QFile existing(target);
    if (!existing.open(QIODevice::ReadOnly) || existing.readAll() != data)
        return std::nullopt;
    return name;
}

bool TagUndo::Private::restorePicture(const KeptPicture &kept, Picture *out) const
{
    QFile file(QDir(scratch).filePath(kept.fileName));
    if (!file.open(QIODevice::ReadOnly))
        return false;
    out->data = file.readAll();
    if (out->data.size() != file.size())
        return false;
    out->type = kept.type;
    out->mimeType = kept.mimeType;
    out->description = kept.description;
    out->byteCount = out->data.size();
    return true;
}

TagUndo::TagUndo(const QString &scratchFolder, QObject *parent)
    : QObject(parent)
    , d(new Private)
{
    d->scratch = scratchFolder;
}

TagUndo::~TagUndo()
{
    d->pending = Step();
    d->committed = Step();
    d->sweep();
    delete d;
}

void TagUndo::begin(const QString &name)
{
    if (d->open)
        end();
    d->open = true;
    d->pending = Step();
    d->pending.name = name;
}

WriteResult TagUndo::write(const QString &path, const TagChanges &changes, const Writer &writer)
{
    const auto save = [&] { return writer ? writer(path, changes) : writeTags(path, changes); };
    if (!d->open)
        return save();

    // Make ready everything that is to be kept, before the writer is called.
    ReadOptions options;
    options.pictures
        = changes.pictures.has_value(); // picture data is read only for a change of pictures
    options.pictureData = options.pictures;
    const ReadResult before = readTags(path, options);
    if (before.status != ReadResult::Status::Ok)
        return refused(QStringLiteral("the earlier tags could not be read"));

    KeptFile kept;
    kept.path = path;
    for (auto it = changes.fields.cbegin(); it != changes.fields.cend(); ++it)
        kept.fields.insert(it.key(), before.tags.fields.value(it.key()));
    for (auto it = changes.other.cbegin(); it != changes.other.cend(); ++it)
        kept.other.insert(it.key(), before.tags.other.value(it.key()));
    if (changes.pictures) {
        QList<KeptPicture> pictures;
        for (const Picture &picture : before.tags.pictures) {
            const std::optional<QString> fileName = d->keepPicture(picture.data);
            if (!fileName) {
                d->sweep();
                return refused(QStringLiteral("the earlier pictures could not be kept"));
            }
            pictures.append({ picture.type, picture.mimeType, picture.description, *fileName });
        }
        kept.pictures = pictures;
    }

    const WriteResult result = save();
    if (result.status != WriteResult::Status::Ok) {
        d->sweep(); // nothing is kept for it, so the picture files made for it go
        return result;
    }

    // A file written twice keeps, for each entry, what it held before the first write that named
    // it.
    if (KeptFile *there = d->pending.find(path)) {
        for (auto it = kept.fields.cbegin(); it != kept.fields.cend(); ++it) {
            if (!there->fields.contains(it.key()))
                there->fields.insert(it.key(), it.value());
        }
        for (auto it = kept.other.cbegin(); it != kept.other.cend(); ++it) {
            if (!there->other.contains(it.key()))
                there->other.insert(it.key(), it.value());
        }
        if (kept.pictures && !there->pictures)
            there->pictures = kept.pictures;
        d->sweep();
        return result;
    }
    // The first file kept in a new step forgets the step before it.
    if (d->pending.files.isEmpty())
        d->committed = Step();
    d->pending.files.append(kept);
    d->sweep();
    return result;
}

void TagUndo::end()
{
    if (!d->open)
        return;
    d->open = false;
    if (d->pending.files.isEmpty()) {
        d->pending = Step(); // kept nothing: the earlier step stays as it was
        return;
    }
    d->committed = d->pending;
    d->pending = Step();
    emit changed();
}

bool TagUndo::canUndo() const
{
    return !d->open && !d->committed.files.isEmpty();
}

QString TagUndo::name() const
{
    return canUndo() ? d->committed.name : QString();
}

QStringList TagUndo::paths() const
{
    QStringList paths;
    if (!canUndo())
        return paths;
    for (const KeptFile &file : d->committed.files)
        paths.append(file.path);
    return paths;
}

TagUndo::Outcome TagUndo::undo(const Writer &writer)
{
    Outcome outcome;
    if (!canUndo())
        return outcome;
    const Step step = d->committed;
    for (const KeptFile &file : step.files) {
        TagChanges change;
        change.fields = file.fields;
        change.other = file.other;
        bool ready = true;
        if (file.pictures) {
            QList<Picture> pictures;
            for (const KeptPicture &kept : *file.pictures) {
                Picture picture;
                if (!d->restorePicture(kept, &picture)) {
                    ready = false;
                    break;
                }
                pictures.append(picture);
            }
            change.pictures = pictures;
        }
        // A picture that cannot be read back is not written over the file: the file is named as
        // failed.
        const WriteResult result = !ready
            ? refused(QStringLiteral("the earlier pictures could not be read"))
            : (writer ? writer(file.path, change) : writeTags(file.path, change));
        (result.ok() ? outcome.restored : outcome.failed).append(file.path);
    }
    d->committed = Step(); // forgotten, whatever the results
    d->sweep();
    emit changed();
    return outcome;
}

// UN-MOVED. fix-83: to be built.
void TagUndo::moved(const QHash<QString, QString> &oldToNew)
{
    Q_UNUSED(oldToNew);
}

}
