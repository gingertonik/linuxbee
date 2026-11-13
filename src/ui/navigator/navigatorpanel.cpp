#include "navigator/navigatorpanel.h"

#include <QAbstractItemView>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

namespace linuxbee::ui {

namespace {

// The node id of a row that can be chosen. Headings carry no id.
const int kNodeRole = Qt::UserRole;

const QStringList kSections = { QStringLiteral("Music"), QStringLiteral("Inbox"),
    QStringLiteral("Audiobooks"), QStringLiteral("Podcasts"), QStringLiteral("Videos") };

// V-LIBRARY: thousands separated by commas, written here so locales do not change it.
QString withCommas(int value)
{
    const QString digits = QString::number(value);
    QString out;
    const int n = digits.size();
    for (int i = 0; i < n; ++i) {
        if (i > 0 && (n - i) % 3 == 0)
            out.append(QLatin1Char(','));
        out.append(digits.at(i));
    }
    return out;
}

QString sectionLabel(const QString &name, int count)
{
    if (count <= 0)
        return name;
    return name + QStringLiteral(" (") + withCommas(count) + QLatin1Char(')');
}

QTreeWidgetItem *addRow(
    QTreeWidget *tree, QTreeWidgetItem *parent, const QString &text, const QString &nodeId)
{
    QTreeWidgetItem *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
    item->setText(0, text);
    if (nodeId.isEmpty()) {
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
    } else {
        item->setData(0, kNodeRole, nodeId);
    }
    return item;
}

// V-PLAYLISTS: "/Rock//Best of/" becomes "Rock", "Best of"; parts are trimmed and empty parts
// dropped.
QStringList cleanedParts(const QString &name)
{
    QStringList parts;
    for (const QString &part : name.split(QLatin1Char('/'))) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty())
            parts.append(trimmed);
    }
    return parts;
}

// Exact match on the id. An empty id matches nothing, so headings are never found.
QTreeWidgetItem *findNode(QTreeWidget *tree, const QString &nodeId)
{
    if (nodeId.isEmpty())
        return nullptr;
    for (QTreeWidgetItemIterator it(tree); *it; ++it) {
        if ((*it)->data(0, kNodeRole).toString() == nodeId)
            return *it;
    }
    return nullptr;
}

} // namespace

struct NavigatorPanel::Private {
    QTreeWidget *tree = nullptr;
    QTreeWidgetItem *playlistsRoot = nullptr;
    QMap<QString, int> counts;
    QString current;
};

NavigatorPanel::NavigatorPanel(QWidget *parent)
    : QWidget(parent)
    , d(new Private)
{
    d->tree = new QTreeWidget(this);
    d->tree->setObjectName(QStringLiteral("navigatorTree"));
    d->tree->setColumnCount(1);
    d->tree->setHeaderHidden(true);
    d->tree->setSelectionMode(QAbstractItemView::SingleSelection);
    d->tree->setEditTriggers(QAbstractItemView::NoEditTriggers);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(d->tree);

    // V-LIBRARY: the fixed rows. Every group is open.
    addRow(d->tree, nullptr, QStringLiteral("Playing Tracks"), QStringLiteral("playing"));

    QTreeWidgetItem *library = addRow(d->tree, nullptr, QStringLiteral("Library"), QString());
    for (const QString &name : kSections)
        addRow(d->tree, library, sectionLabel(name, 0), QStringLiteral("library:") + name);
    library->setExpanded(true);

    d->playlistsRoot = addRow(d->tree, nullptr, QStringLiteral("Playlists"), QString());
    d->playlistsRoot->setExpanded(true);

    // V-NODES: the panel opens on the music library.
    d->current = QStringLiteral("library:Music");
    d->tree->setCurrentItem(findNode(d->tree, d->current));
}

NavigatorPanel::~NavigatorPanel()
{
    delete d;
}

void NavigatorPanel::setLibraryCounts(const QMap<QString, int> &counts)
{
    d->counts = counts;
    const QString prefix = QStringLiteral("library:");
    for (QTreeWidgetItemIterator it(d->tree); *it; ++it) {
        const QString nodeId = (*it)->data(0, kNodeRole).toString();
        if (nodeId.startsWith(prefix)) {
            const QString name = nodeId.mid(prefix.size());
            (*it)->setText(0, sectionLabel(name, d->counts.value(name, 0)));
        }
    }
}

void NavigatorPanel::setPlaylists(const QStringList &names)
{
    // Replace the whole list: drop the old rows under Playlists, then build the new ones.
    qDeleteAll(d->playlistsRoot->takeChildren());

    QStringList seenPaths;
    QMap<QString, QTreeWidgetItem *> folders; // folder path ("Rock/Live") to its row
    for (const QString &name : names) {
        const QStringList parts = cleanedParts(name);
        if (parts.isEmpty())
            continue;
        const QString path = parts.join(QLatin1Char('/'));
        if (seenPaths.contains(path))
            continue;
        seenPaths.append(path);

        QTreeWidgetItem *parent = d->playlistsRoot;
        QString folderPath;
        for (int i = 0; i < parts.size() - 1; ++i) {
            folderPath
                = folderPath.isEmpty() ? parts.at(i) : folderPath + QLatin1Char('/') + parts.at(i);
            QTreeWidgetItem *folder = folders.value(folderPath, nullptr);
            if (!folder) {
                folder = addRow(d->tree, parent, parts.at(i), QString());
                folder->setExpanded(true);
                folders.insert(folderPath, folder);
            }
            parent = folder;
        }
        addRow(d->tree, parent, parts.last(), QStringLiteral("playlist:") + path);
    }
}

QString NavigatorPanel::currentNode() const
{
    return d->current;
}

bool NavigatorPanel::selectNode(const QString &nodeId)
{
    QTreeWidgetItem *item = findNode(d->tree, nodeId);
    if (!item)
        return false;
    d->current = nodeId;
    d->tree->setCurrentItem(item);
    return true;
}

QStringList NavigatorPanel::nodes() const
{
    QStringList out;
    for (QTreeWidgetItemIterator it(d->tree); *it; ++it) {
        const QString nodeId = (*it)->data(0, kNodeRole).toString();
        if (!nodeId.isEmpty())
            out.append(nodeId);
    }
    return out;
}

}
