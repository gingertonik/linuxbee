#include "tagundo/tagundo.h"

// Stub from the seed: it writes and remembers nothing. The builder replaces this file.
namespace linuxbee {

struct TagUndo::Private {
    QString scratch;
};

TagUndo::TagUndo(const QString &scratchFolder, QObject *parent)
    : QObject(parent)
    , d(new Private { scratchFolder })
{
}

TagUndo::~TagUndo()
{
    delete d;
}

void TagUndo::begin(const QString &name)
{
    (void)name;
}

WriteResult TagUndo::write(const QString &path, const TagChanges &changes, const Writer &writer)
{
    return writer ? writer(path, changes) : writeTags(path, changes);
}

void TagUndo::end()
{
}

bool TagUndo::canUndo() const
{
    return false;
}

QString TagUndo::name() const
{
    return { };
}

QStringList TagUndo::paths() const
{
    return { };
}

TagUndo::Outcome TagUndo::undo(const Writer &writer)
{
    (void)writer;
    return { };
}

}
