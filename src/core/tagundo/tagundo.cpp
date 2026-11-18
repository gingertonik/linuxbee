#include "tagundo/tagundo.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSet>

#include <optional>

#include <sys/stat.h>

// Contract: docs/spec/tag-undo.md (UN-RECORD, UN-STEP, UN-SAME, UN-MOVED, UN-RATING).
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

// UN-SAME: what a file is, as `stat` says it: device, inode, size and modification time
// (with the nanoseconds).
struct Identity {
    quint64 device = 0;
    quint64 inode = 0;
    qint64 size = 0;
    qint64 seconds = 0;
    qint64 nanoseconds = 0;

    bool operator==(const Identity &other) const
    {
        return device == other.device && inode == other.inode && size == other.size
            && seconds == other.seconds && nanoseconds == other.nanoseconds;
    }
};

// What is at `path` now. Nothing when `stat` finds nothing there.
std::optional<Identity> identityOf(const QString &path)
{
    struct stat info { };
    if (::stat(QFile::encodeName(path).constData(), &info) != 0)
        return std::nullopt;
    Identity identity;
    identity.device = quint64(info.st_dev);
    identity.inode = quint64(info.st_ino);
    identity.size = qint64(info.st_size);
    identity.seconds = qint64(info.st_mtim.tv_sec);
    identity.nanoseconds = qint64(info.st_mtim.tv_nsec);
    return identity;
}

// What the entries one file's changes named held before the first write that named them.
struct KeptFile {
    QString path;
    // The file as the last write of the step left it. Nothing when it could not be read.
    std::optional<Identity> identity;
    QMap<QString, QStringList> fields;
    QMap<QString, QStringList> other;
    std::optional<QList<KeptPicture>> pictures;
    // UN-RATING: every POPM frame as it was, when the change named Rating on an ID3v2 file.
    std::optional<QList<RatingFrame>> ratingFrames;
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
    // UN-RATING: the frames themselves, not the stars they read as.
    if (changes.fields.contains(QStringLiteral("Rating")))
        kept.ratingFrames = readRatingFrames(path);
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
    // UN-SAME: what the file is, straight after the write the step keeps it for.
    kept.identity = identityOf(path);

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
        if (kept.ratingFrames && !there->ratingFrames)
            there->ratingFrames = kept.ratingFrames;
        there->identity = kept.identity; // as the last write left it
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
    // UN-KEEP: what is left of the step, each file as it was kept and in its order.
    Step left;
    left.name = step.name;
    for (const KeptFile &file : step.files) {
        // UN-SAME: something at the path that is not the file that was edited is not written to.
        // Nothing at the path goes to the writer as before, which fails.
        if (const std::optional<Identity> now = identityOf(file.path)) {
            if (!file.identity || !(*now == *file.identity)) {
                outcome.changed.append(file.path);
                left.files.append(file);
                continue;
            }
        }
        TagChanges change;
        change.fields = file.fields;
        change.other = file.other;
        change.ratingFrames = file.ratingFrames;
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
        if (result.ok()) {
            outcome.restored.append(file.path);
        } else {
            outcome.failed.append(file.path);
            left.files.append(file);
        }
    }
    // A file put back leaves the step; with none left, the step is forgotten.
    d->committed = left.files.isEmpty() ? Step() : left;
    d->sweep();
    emit changed();
    return outcome;
}

// UN-MOVED: every file of the pending and of the committed step whose path is a key gets the new
// path. Where the file at the new path has the size that was kept, what it is is taken from
// there (a move to another disk is a copy, and a copy is another inode); where not, nothing is
// taken, and UN-SAME finds it changed.
void TagUndo::moved(const QHash<QString, QString> &oldToNew)
{
    if (oldToNew.isEmpty())
        return;
    bool pathsChanged = false;
    for (Step *step : { &d->pending, &d->committed }) {
        for (KeptFile &file : step->files) {
            const auto it = oldToNew.constFind(file.path);
            if (it == oldToNew.constEnd())
                continue;
            file.path = it.value();
            if (step == &d->committed)
                pathsChanged = true;
            const std::optional<Identity> there = identityOf(file.path);
            if (there && file.identity && there->size == file.identity->size)
                file.identity = there;
        }
    }
    if (pathsChanged)
        emit changed();
}

}
